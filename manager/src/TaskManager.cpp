#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/atomic.hpp>
#include <glog/logging.h>

#define LOG_HEADER "TaskManager"

#include "TaskEnv.h"
#include "Task.h"
#include "Block.h"
#include "TaskQueue.h"
#include "TaskManager.h"
#include "Platform.h"

namespace blaze {

int TaskManager::getExeQueueLength() {
  return exeQueueLength.load();
}

int TaskManager::estimateTime(Task* task) {

  // check if time estimation is already stored
  if (task->estimated_time > 0) {
    return task->estimated_time;
  }
  else {
    int task_delay = task->estimateTime();
    int ret_delay = 0;

    if (task_delay <= 0) { // no estimation
      // TODO: use a regression model
      // current implementation just return a constant
      ret_delay = 1e5;
    }
    else {
      // TODO: apply an estimation model (linear regression)
      ret_delay = task_delay + deltaDelay;
    }
    // store the time estimation in task
    task->estimated_time = ret_delay;

    return ret_delay;
  }
}

void TaskManager::updateDelayModel(
    Task* task, 
    int estimateTime, int realTime) 
{
  ;
}

Task_ptr TaskManager::create() {
  
  // create a new task by the constructor loaded form user implementation
  Task_ptr task(createTask(), destroyTask);

  // link the TaskEnv
  task->env = platform->getEnv(acc_id);

  // give task an unique ID
  task->task_id = nextTaskId.fetch_add(1);

  return task;
}

void TaskManager::enqueue(std::string app_id, Task* task) {

  if (!task->isReady()) {
    throw std::runtime_error("Cannot enqueue task that is not ready");
  }
  
  // TODO: when do we remove the queue?
  // create a new app queue if it does not exist

  TaskQueue_ptr queue;
  // TODO: remove this lock
  this->lock();
  if (app_queues.find(app_id) == app_queues.end()) {
    TaskQueue_ptr new_queue(new TaskQueue());
    app_queues.insert(std::make_pair(app_id, new_queue));
    queue = new_queue;
  } 
  else {
    queue = app_queues[app_id];
  }
  this->unlock();

  // once called, the task estimation time will stored
  int delay_time = estimateTime(task);

  // push task to queue
  bool enqueued = queue->push(task);
  while (!enqueued) {
    boost::this_thread::sleep_for(boost::chrono::microseconds(100)); 
    enqueued = queue->push(task);
  }
  
  // update lobby wait time
  lobbyWaitTime.fetch_add(delay_time);

  // update door wait time
  doorWaitTime.fetch_sub(delay_time);
}

void TaskManager::schedule() {

  // iterate through all app queues and record which are non-empty
  std::vector<std::string> ready_queues;
  std::map<std::string, TaskQueue_ptr>::iterator iter;

  while (ready_queues.empty()) {
    for (iter = app_queues.begin();
        iter != app_queues.end();
        iter ++)
    {
      if (iter->second && !iter->second->empty()) {
        ready_queues.push_back(iter->first);
      }
    }
    if (ready_queues.empty()) {
      boost::this_thread::sleep_for(boost::chrono::microseconds(1000)); 
    }
  }
  Task* next_task;

  // select the next task to execute from application queues
  // use RoundRobin scheduling
  int idx_next = rand()%ready_queues.size();

  if (app_queues.find(ready_queues[idx_next]) == app_queues.end()) {
    LOG(ERROR) << "Did not find app_queue, unexpected";
    return;
  }
  app_queues[ready_queues[idx_next]]->pop(next_task);

  execution_queue.push(next_task);

  // atomically increase the length of the task queue
  exeQueueLength.fetch_add(1);

  VLOG(1) << "Schedule a task to execute from " << ready_queues[idx_next];
}

bool TaskManager::popReady(Task* &task) {
  if (execution_queue.empty()) {
    return false;
  }
  else {
    execution_queue.pop(task);
    return true;
  }
}

void TaskManager::execute() {

  // wait if there is no task to be executed
  while (execution_queue.empty()) {
    boost::this_thread::sleep_for(boost::chrono::microseconds(100)); 
  }
  // get next task and remove it from the task queue
  // this part is thread-safe with boost::lockfree::queue
  Task* task;
  execution_queue.pop(task);

  int delay_estimate = estimateTime(task);

  VLOG(1) << "Started a new task";

  try {
    // record task execution time
    uint64_t start_time = getUs();

    // start execution
    task->execute();
    uint64_t delay_time = getUs() - start_time;

    VLOG(1) << "Task finishes in " << delay_time << " us";

    // if the task is successful, update delay estimation model
    if (task->status == Task::FINISHED) {
      updateDelayModel(task, delay_estimate, delay_time);
    }

    // decrease the waittime, use the recorded estimation 
    lobbyWaitTime.fetch_sub(delay_estimate);

    // decrease the length of the execution queue
    exeQueueLength.fetch_sub(1);
  } 
  catch (std::runtime_error &e) {
    LOG(ERROR) << "Task error " << e.what();
  }
}

std::pair<int, int> TaskManager::getWaitTime(Task* task) {

  // increment door with current task time
  int currDoorWaitTime  = doorWaitTime.fetch_add(estimateTime(task));
  int currLobbyWaitTime = lobbyWaitTime.load();

  return std::make_pair(
      currLobbyWaitTime, currLobbyWaitTime + currDoorWaitTime);
}

std::string TaskManager::getConfig(int idx, std::string key) {
  Task* task = (Task*)createTask();

  std::string config = task->getConfig(idx, key);

  destroyTask(task);
  
  return config;
}

void TaskManager::do_execute() {

  LOG(INFO) << "Started an executor";

  // continuously execute tasks from the task queue
  while (1) { 
    execute();
  }
}

void TaskManager::do_schedule() {
  
  LOG(INFO) << "Started an scheduler";

  while (1) {
    schedule();
  }
}

void TaskManager::start() {
  startExecutor();
  startScheduler();
}

void TaskManager::startExecutor() {
  boost::thread executor(
      boost::bind(&TaskManager::do_execute, this));
}

void TaskManager::startScheduler() {
  boost::thread scheduler(
      boost::bind(&TaskManager::do_schedule, this));
}

} // namespace blaze
