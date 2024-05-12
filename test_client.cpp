// ConsoleApplication2.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "perf.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <sstream>
#include <system_error>
#include <vector>
#include <memory>
#include <iostream>
namespace {
    struct Timestamps{
        std::uint64_t clitt;
        std::uint64_t sendt;
        std::uint64_t rcvt;
    };
    constexpr std::size_t MaxSamples = 102400;
    class UnaryClient {
    public:
        explicit UnaryClient(std::shared_ptr<::grpc::Channel>& channel,
            const std::string& name, size_t payloadSize,
            std::uint64_t msgPerSec, size_t publishDuration) :
            _stubs(4),
            _name(name),
            _msgPerSec{ msgPerSec },
            _payload(payloadSize, 'x'),
            _publishDuration(publishDuration),
            _counter(0) {
            gpr_timespec t_out{ 3,0, GPR_TIMESPAN };
            bool connected = channel->WaitForConnected(t_out);
            if (!connected) {
                std::cout << "cannot connect!" << std::endl;
            }
            for (size_t i = 0; i < _stubs.size(); i++) {

                _stubs[i] = proto::PerfTest::NewStub(channel);
             }
            _result.reserve(_msgPerSec*_publishDuration);
            _cqhandleThread = std::thread(&UnaryClient::AsyncCompleteRpc, this);
        }
        ~UnaryClient() {
            if (_senderThread.joinable())
                _senderThread.join();
            _cq.Shutdown();
            _cqhandleThread.join();
        }
        void start() {
            _senderThread = std::thread(&UnaryClient::runpublish, this);
        }
        void runpublish() {
            std::chrono::nanoseconds interval(static_cast<size_t>(1e9 / (double)_msgPerSec));
            std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
            std::chrono::high_resolution_clock::time_point deadline = startTime + interval;
            int index = 0;
            for (size_t i = 0; i < _msgPerSec * _publishDuration; i++) {
                deadline = deadline + interval;
                index++;
                if (index == 4) index = 0;
                std::this_thread::sleep_until(deadline);
                auto* call = new AsyncClientCall(_payload, *this);
                call -> reader = _stubs[index]->AsyncUnary(&call->context, call->request, &_cq);
                call->reader -> Finish(&call->response, &call->status, (void*)call);
            }
            while (_counter.load() != _msgPerSec * _publishDuration) {
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            std::cout << "publish time" << (_result.front().clitt) / 1000000000
                << "." << _result.front().clitt % 1000000000 << " to "
                << (_result.back().clitt) / 1000000000
                << "." << _result.back().clitt % 1000000000 << ", duration is"
                << (double)(_result.back().clitt - _result.front().sendt) / 1000000000;
            std::cout << "server recv time" << (_result.front().sendt) / 1000000000
                << "." << _result.front().sendt % 1000000000 << " to "
                << (_result.back().sendt) / 1000000000
                << "." << _result.back().sendt % 1000000000 << ", duration is"
                << (double)(_result.back().sendt - _result.front().sendt) / 1000000000;
            std::cout << "client recv time" << (_result.front().rcvt) / 1000000000
                << "." << _result.front().rcvt % 1000000000 << " to "
                << (_result.back().rcvt) / 1000000000
                << "." << _result.back().rcvt % 1000000000 << ", duration is"
                << (double)(_result.back().rcvt - _result.front().rcvt) / 1000000000;
        }

    private:
        struct AsyncClientCall {
            proto::Message request;
            proto::Message response;
            grpc::ClientContext context;
            grpc::Status status;
            std::unique_ptr<grpc::ClientAsyncResponseReader<proto::Message>> reader;
            UnaryClient& client;
            AsyncClientCall(const std::string payload, UnaryClient& client_) :
                client(client_) {
                request.set_payload(payload);
                request.set_clittime(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
            }
            void process() {
                if (!status.ok())
                    return;
                client._counter++;
                client._result.emplace_back(Timestamps{ response.clittime(), response.sendtime(),
                    static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())});
                    delete this;
            }
        };
        void AsyncCompleteRpc() {
            void* got_tag;
            bool ok = false;
            while (_cq.Next(&got_tag, &ok)) {
                AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
                call->process();
            }
        }
        std::vector<std::unique_ptr<proto::PerfTest::Stub>> _stubs;
        std::string _name;
        ::grpc::CompletionQueue _cq;
        uint64_t _msgPerSec;
        std::string _payload;
        uint64_t _publishDuration;
        std::vector<Timestamps> _result;
        std::thread _senderThread;
        std::thread _cqhandleThread;
        std::atomic<uint64_t> _counter;
    };
}
int main(int argc,const char* argv[])
{
    std::string addr="localhost:44444";
    std::uint64_t msgSize, msgsPerSec, publishTime, cnt;
    msgSize=1024;
    msgsPerSec=1000;
    publishTime=10;
    cnt=1;
    std::vector <std::unique_ptr<UnaryClient>> clients;
    for (size_t k = 0; k < cnt; k++) {
        grpc::ChannelArguments ch_args;
        ch_args.SetInt("shard_to_ensure_no_subschannel_merges", k);
        auto channel=grpc::CreateCustomChannel(addr, grpc::InsecureChannelCredentials(), ch_args);
        clients.emplace_back(std::make_unique<UnaryClient>(channel, std::string("publisher:") + std::to_string(k), msgSize, msgsPerSec, publishTime));
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    for (auto& client : clients){
        client->start();
    }
    return 0;
}
