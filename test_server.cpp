#include "test_server.h"
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <sstream>
#include <system_error>
using grpc::Server;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using proto::Message;

Status ProcessSimpleRPC(const proto::Message* request, proto::Message* response)
{
	response->set_clittime(request->clittime());
	response->set_sendtime(static_cast<std::uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count()));
	response->set_payload(request->payload());
	return Status::OK;
}

ServerRpcContextPerf::ServerRpcContextPerf(
		std::function<void(grpc::ServerContext*, proto::Message*,
			grpc::ServerAsyncResponseWriter<proto::Message>*, void*)> request_method,
		std::function<grpc::Status(const proto::Message*, proto::Message*)> invoke_method_)
	: srv_ctx_(new ServerContext),
	request_method_(request_method),
	invoke_method(invoke_method_),
	response_writer(srv_ctx_.get())
{
	next_state_ =
		std::bind(&ServerRpcContextPerf::invoker, this, std::placeholders::_1);
	request_method_(srv_ctx_.get(), &req_, &response_writer, (void*)this);
}

ServerRpcContextPerf::~ ServerRpcContextPerf()
{
}

bool ServerRpcContextPerf::RunNextState(bool ok)
{
	return next_state_(ok);
}
void ServerRpcContextPerf::Reset()
{
	srv_ctx_.reset(new ServerContext);
	req_ = proto::Message();
	response_writer = grpc::ServerAsyncResponseWriter<proto::Message>(srv_ctx_.get());
	// Then request the method
	next_state_ =
		std::bind(&ServerRpcContextPerf::invoker, this, std::placeholders::_1);
	request_method_(srv_ctx_.get(), &req_, &response_writer, (void*)this);
}

bool ServerRpcContextPerf::finisher(bool)
{
	return false;
}
bool ServerRpcContextPerf::invoker(bool ok)
{
	if (!ok)
	{
		return false;
	}
	proto::Message response;
	grpc::Status status = invoke_method(&req_, &response);
	next_state_ =
		std::bind(&ServerRpcContextPerf::finisher, this, std::placeholders::_1);
	response_writer.Finish(response, status, (void*)this);
	return true;
}
ServerImpl::ServerImpl(rpc_type type_)
{
	num_threads = 8;
	std::string server_address("0.0.0.0:44444");
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service_);
	// Add one completion queue per thread
	for (int i = 0; i < num_threads; i++)
	{
		srv_cqs_.emplace_back(builder.AddCompletionQueue());
	}
	server_ = builder.BuildAndStart();
	// Add a bunch of contexts, 100k per cq
	for (int i = 0; i < 100000 / num_threads; i++)
	{
		for (int j = 0; j < num_threads; j++)
		{
			auto process_rpc_bound = std::bind(ProcessSimpleRPC, std::placeholders::_1, std::placeholders::_2);
			auto request_unary = std::bind(&proto::PerfTest::AsyncService::RequestUnary, &service_,std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
				srv_cqs_[j].get(), srv_cqs_[j].get(), std::placeholders::_4);
			contexts_.push_front(new ServerRpcContextPerf(request_unary, process_rpc_bound));
		}
	}
	for (int i = 0; i < num_threads; i++)
	{
		threads.emplace_back(&ServerImpl::ThreadFunc, this, i);
	}
	std::cout << "ready to receive RPC" << std::endl;
	for (auto thr = threads.begin(); thr != threads.end(); thr++)
	{
		thr->join();
	}
}

ServerImpl::~ServerImpl()
{

	server_->Shutdown();
	for (auto cq = srv_cqs_.begin(); cq != srv_cqs_.end(); ++cq)
	{
		(*cq)->Shutdown();
		bool ok;
		void* got_tag;
		while ((*cq)->Next(&got_tag, &ok))
			;
		while (!contexts_.empty())
		{
			delete contexts_.front();
			contexts_.pop_front();
		}
	}
}

void ServerImpl::ThreadFunc(int rank)
{
	bool ok;
	void* got_tag;
	prctl(PR_SET_NAME, "pubhandleRPCT");
	while (srv_cqs_[rank]->Next(&got_tag, &ok))
	{
		ServerRpcContextPerf* ctx = reinterpret_cast<ServerRpcContextPerf*>(got_tag);
		// The tag is a pointer to an RPC context to invoke
		const bool still_going = ctx->RunNextState(ok);
		if (!still_going)
		{
			ctx->Reset();
		}
	}
	return;
}

int main(int argc, char** argv)
{
	ServerImpl server(ServerImpl::rpc_type::UNARY);
	getchar();
	return 0;
}