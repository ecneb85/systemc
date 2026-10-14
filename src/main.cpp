#include <systemc>
#include <tlm>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <array>
#include <cstdint>
#include <iostream>

using namespace sc_core;
using namespace tlm;

namespace {

const char* command_name(tlm_command command) {
    return command == TLM_READ_COMMAND ? "READ" : "WRITE";
}

void log_trace(const char* module, const char* message) {
    std::cout << sc_time_stamp() << " [" << module << "] " << message << '\n';
}

} // namespace

class ByteMemory : public sc_module {
  public:
    tlm_utils::simple_target_socket<ByteMemory> socket{"socket"};

    SC_HAS_PROCESS(ByteMemory);

    explicit ByteMemory(sc_module_name name)
        : sc_module(name), request_queue_(this, &ByteMemory::request_queue_callback) {
        socket.register_nb_transport_fw(this, &ByteMemory::nb_transport_fw);
    }

  private:
    std::array<unsigned char, 256> memory_{};
    const sc_time request_latency_{5, SC_NS};
    const sc_time response_latency_{10, SC_NS};
    // Queue delayed phase callbacks so transport methods never block.
    tlm_utils::peq_with_cb_and_phase<ByteMemory> request_queue_;

    tlm_sync_enum nb_transport_fw(tlm_generic_payload& transaction, tlm_phase& phase,
                                  sc_time& delay) {
        if (phase == BEGIN_REQ) {
            log_trace("memory", "accepted BEGIN_REQ");
            // End the request phase after the modeled request latency.
            request_queue_.notify(transaction, END_REQ, delay + request_latency_);
            return TLM_ACCEPTED;
        }

        if (phase == END_RESP) {
            log_trace("memory", "accepted END_RESP; transaction is complete");
            return TLM_COMPLETED;
        }

        SC_REPORT_FATAL("ByteMemory", "Unexpected forward transport phase");
        return TLM_COMPLETED;
    }

    void request_queue_callback(tlm_generic_payload& transaction, const tlm_phase& queued_phase) {
        if (queued_phase == END_REQ) {
            tlm_phase phase = END_REQ;
            sc_time delay = SC_ZERO_TIME;
            log_trace("memory", "sending END_REQ");

            const tlm_sync_enum status = socket->nb_transport_bw(transaction, phase, delay);
            if (status != TLM_ACCEPTED) {
                SC_REPORT_FATAL("ByteMemory", "Initiator must accept END_REQ");
                return;
            }

            process_request(transaction);
            // The operation completes before the target presents its response.
            request_queue_.notify(transaction, BEGIN_RESP, response_latency_);
            return;
        }

        if (queued_phase == BEGIN_RESP) {
            tlm_phase phase = BEGIN_RESP;
            sc_time delay = SC_ZERO_TIME;
            log_trace("memory", "sending BEGIN_RESP");

            const tlm_sync_enum status = socket->nb_transport_bw(transaction, phase, delay);
            if (status != TLM_ACCEPTED) {
                SC_REPORT_FATAL("ByteMemory", "Initiator must accept BEGIN_RESP");
            }
            return;
        }

        SC_REPORT_FATAL("ByteMemory", "Unexpected phase in target event queue");
    }

    void process_request(tlm_generic_payload& transaction) {
        const auto address = transaction.get_address();
        if (transaction.get_data_length() != 1 || address >= memory_.size()) {
            transaction.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        unsigned char* data = transaction.get_data_ptr();
        if (data == nullptr) {
            transaction.set_response_status(TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        if (transaction.get_command() == TLM_WRITE_COMMAND) {
            memory_[address] = *data;
        } else if (transaction.get_command() == TLM_READ_COMMAND) {
            *data = memory_[address];
        } else {
            transaction.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        transaction.set_response_status(TLM_OK_RESPONSE);
        log_trace("memory", "completed the memory operation");
    }
};

class Cpu : public sc_module {
  public:
    tlm_utils::simple_initiator_socket<Cpu> socket{"socket"};

    SC_HAS_PROCESS(Cpu);

    explicit Cpu(sc_module_name name)
        : sc_module(name), response_queue_(this, &Cpu::response_queue_callback) {
        socket.register_nb_transport_bw(this, &Cpu::nb_transport_bw);
        SC_THREAD(run);
    }

  private:
    const sc_time end_response_delay_{2, SC_NS};
    tlm_utils::peq_with_cb_and_phase<Cpu> response_queue_;
    sc_event transaction_done_;

    tlm_sync_enum nb_transport_bw(tlm_generic_payload& transaction, tlm_phase& phase,
                                  sc_time& delay) {
        if (phase == END_REQ) {
            log_trace("cpu", "received END_REQ; request phase is over");
            return TLM_ACCEPTED;
        }

        if (phase == BEGIN_RESP) {
            log_trace("cpu", "received BEGIN_RESP; scheduling END_RESP");
            // Accept the response now; send END_RESP after the modeled delay.
            response_queue_.notify(transaction, END_RESP, delay + end_response_delay_);
            return TLM_ACCEPTED;
        }

        SC_REPORT_FATAL("Cpu", "Unexpected backward transport phase");
        return TLM_COMPLETED;
    }

    void response_queue_callback(tlm_generic_payload& transaction, const tlm_phase& phase) {
        if (phase != END_RESP) {
            SC_REPORT_FATAL("Cpu", "Unexpected phase in initiator event queue");
            return;
        }

        tlm_phase forward_phase = END_RESP;
        sc_time delay = SC_ZERO_TIME;
        log_trace("cpu", "sending END_RESP");

        const tlm_sync_enum status = socket->nb_transport_fw(transaction, forward_phase, delay);
        if (status != TLM_COMPLETED) {
            SC_REPORT_FATAL("Cpu", "Target must complete the transaction at END_RESP");
            return;
        }

        // Wake transact() only after the target has released the payload.
        transaction_done_.notify(SC_ZERO_TIME);
    }

    void transact(tlm_command command, std::uint64_t address, unsigned char& data) {
        // Both objects are stack-backed, so wait here until the target completes.
        tlm_generic_payload transaction;
        transaction.set_command(command);
        transaction.set_address(address);
        transaction.set_data_ptr(&data);
        transaction.set_data_length(1);
        transaction.set_streaming_width(1);
        transaction.set_byte_enable_ptr(nullptr);
        transaction.set_byte_enable_length(0);
        transaction.set_dmi_allowed(false);
        transaction.set_response_status(TLM_INCOMPLETE_RESPONSE);

        tlm_phase phase = BEGIN_REQ;
        sc_time delay = SC_ZERO_TIME;
        log_trace("cpu", command_name(command));

        const tlm_sync_enum status = socket->nb_transport_fw(transaction, phase, delay);
        if (status != TLM_ACCEPTED) {
            SC_REPORT_FATAL("Cpu", "Target must accept BEGIN_REQ");
            return;
        }

        wait(transaction_done_);
        if (transaction.is_response_error()) {
            SC_REPORT_FATAL("Cpu", transaction.get_response_string().c_str());
            return;
        }

        std::cout << sc_time_stamp() << " [cpu] transaction result: 0x" << std::hex
                  << static_cast<unsigned int>(data) << std::dec << '\n';
    }

    void run() {
        wait(1, SC_NS);

        unsigned char value = 0x2A;
        transact(TLM_WRITE_COMMAND, 0x10, value);

        value = 0;
        transact(TLM_READ_COMMAND, 0x10, value);
        if (value != 0x2A) {
            SC_REPORT_FATAL("Cpu", "Readback did not match the written value");
        }
    }
};

int sc_main(int, char**) {
    sc_set_time_resolution(1, SC_NS);

    Cpu cpu("cpu");
    ByteMemory memory("memory");
    cpu.socket.bind(memory.socket);

    sc_start();
    std::cout << "Simulation finished successfully.\n";
    return 0;
}

#if defined(TLM_AT_SYSTEMC_DLL)
int main(int argc, char** argv) {
    return sc_main(argc, argv);
}
#endif
