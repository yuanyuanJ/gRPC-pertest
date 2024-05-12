#pragma once
#include <forward_list>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <functional>
#include <vector>
#include <grpc++/grpc++.h>
#include "perf.grpc.pb.h"
#include <grpc++/impl/codegen/async_stream.h>

class ServerRpcContextPerf {
public:
	ServerRpcContextPerf(
		std::function<void(grpc::ServerContext*, proto::Message*,
			grpc::ServerAsyncResponseWriter<proto::Message>*, void*)> request_method,
		std::function<grpc::Status(const proto::Message*, proto::Message*)> invoke_method);
	~ServerRpcContextPerf();
	bool RunNextState(bool ok);
	void Reset();
private:
	bool finisher(bool);
	bool invoker(bool ok);
	std::unique_ptr<grpc::ServerContext> srv_ctx_;
	proto::Message req_;
	std::function<bool(bool)> next_state_;
	std::function<void(grpc::ServerContext*, proto::Message*,
		grpc::ServerAsyncResponseWriter<proto::Message>*, void*)> request_method_;
	std::function<grpc::Status(const proto::Message*, proto::Message*)> invoke_method;
	grpc::ServerAsyncResponseWriter<proto::Message> response_writer;
};
class ServerImpl {
public:
	enum rpc_type {
		UNARY = 0,
	};
	ServerImpl(rpc_type type_);
	~ServerImpl();
	void ThreadFunc(int rank);
private:
	grpc::ServerBuilder builder;
	std::vector<std::unique_ptr<grpc::ServerCompletionQueue>> srv_cqs_;
	std::forward_list<ServerRpcContextPerf*> contexts_;
	std::vector<std::thread> threads;
	proto::PerfTest::AsyncService service_;
	std::unique_ptr<grpc::Server> server_;
	int num_threads;
};