#include <time.h>
#include <faabric/executor/ExecutorFactory.h>
#include <faabric/scheduler/FunctionCallServer.h>
#include <faabric/snapshot/SnapshotRegistry.h>
#include <faabric/state/State.h>
#include <faabric/transport/common.h>
#include <faabric/transport/macros.h>
#include <faabric/util/config.h>
#include <faabric/util/func.h>
#include <faabric/util/logging.h>

std::map<int, struct timespec> worker_get_ts;
std::map<int, long> worker_enqueue_cost;

namespace faabric::scheduler {
FunctionCallServer::FunctionCallServer()
  : faabric::transport::MessageEndpointServer(
      FUNCTION_CALL_ASYNC_PORT,
      FUNCTION_CALL_SYNC_PORT,
      FUNCTION_INPROC_LABEL,
      faabric::util::getSystemConfig().functionServerThreads)
  , scheduler(getScheduler())
{}

void FunctionCallServer::doAsyncRecv(transport::Message& message)
{
    uint8_t header = message.getMessageCode();
    switch (header) {
        case faabric::scheduler::FunctionCalls::ExecuteFunctions: {
            recvExecuteFunctions(message.udata());
            break;
        }
        case faabric::scheduler::FunctionCalls::SetMessageResult: {
            recvSetMessageResult(message.udata());
            break;
        }
        default: {
            throw std::runtime_error(
              fmt::format("Unrecognized async call header: {}", header));
        }
    }
}

std::unique_ptr<google::protobuf::Message> FunctionCallServer::doSyncRecv(
  transport::Message& message)
{
    uint8_t header = message.getMessageCode();
    switch (header) {
        case faabric::scheduler::FunctionCalls::Flush: {
            return recvFlush(message.udata());
        }
        default: {
            throw std::runtime_error(
              fmt::format("Unrecognized sync call header: {}", header));
        }
    }
}

std::unique_ptr<google::protobuf::Message> FunctionCallServer::recvFlush(
  std::span<const uint8_t> buffer)
{
    SPDLOG_INFO("Flushing host {}",
                faabric::util::getSystemConfig().endpointHost);

    // Clear out any cached state
    faabric::state::getGlobalState().forceClearAll(false);

    // Clear the scheduler
    faabric::scheduler::getScheduler().reset();

    // Clear the executor factory
    faabric::executor::getExecutorFactory()->flushHost();

    return std::make_unique<faabric::EmptyResponse>();
}

void FunctionCallServer::recvExecuteFunctions(std::span<const uint8_t> buffer)
{
    struct timespec arrival_ts;
    clock_gettime(CLOCK_MONOTONIC, &arrival_ts);
    long before_enqueue_request_t = faabric::util::getGlobalClock().epochMicros();
    PARSE_MSG(faabric::BatchExecuteRequest, buffer.data(), buffer.size())
    worker_get_ts[parsedMsg.appid()] = arrival_ts;

    // This host has now been told to execute these functions no matter what
    for (int i = 0; i < parsedMsg.messages_size(); i++) {
        parsedMsg.mutable_messages()->at(i).set_executedhost(
          faabric::util::getSystemConfig().endpointHost);
    }
    scheduler.executeBatch(
      std::make_shared<faabric::BatchExecuteRequest>(parsedMsg));
    long after_enqueue_request_t = faabric::util::getGlobalClock().epochMicros();
    long enqueue_request_us = after_enqueue_request_t - before_enqueue_request_t;
    worker_enqueue_cost[parsedMsg.appid()] = enqueue_request_us;
}

void FunctionCallServer::recvSetMessageResult(std::span<const uint8_t> buffer)
{
    PARSE_MSG(faabric::Message, buffer.data(), buffer.size())
    faabric::planner::getPlannerClient().setMessageResultLocally(
      std::make_shared<faabric::Message>(parsedMsg));
}
}
