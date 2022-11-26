#include <algorithm>
#include <random>
#include <vector>
#include <string>
#include <iostream>

#include "bwtree.h"
#include "bwtree_test_util.h"
#include "multithread_test_util.h"
#include "timer.h"
#include "worker_pool.h"
#include "zipf.h"

std::atomic<size_t> insert_success_counter = 0;
std::atomic<size_t> total_op_counter = 0;
std::atomic<size_t> total_read_counter = 0;

uint32_t skewedInsertWorkload(uint32_t id, uint32_t key_num,
  test::BwTreeTestUtil::TreeType* tree)
{
  std::default_random_engine thread_generator(id);
  std::normal_distribution<double> dist(0, 0.01);
  std::uniform_int_distribution<int> value_dist(0, key_num);
  uint32_t op_cnt = 0;

  while (insert_success_counter.load() < key_num) {
    const int key = key_num * dist(thread_generator);
    const int value = value_dist(thread_generator);

    if (tree->Insert(key, value)) insert_success_counter.fetch_add(1);
    op_cnt++;
  }

  return op_cnt;
}

uint32_t uniformInsertWorkload(uint32_t id, uint32_t key_num,
  test::BwTreeTestUtil::TreeType* tree)
{
  std::default_random_engine thread_generator(id);
  std::uniform_int_distribution<int> dist(0, key_num);
  std::uniform_int_distribution<int> value_dist(0, key_num);
  uint32_t op_cnt = 0;

  while (insert_success_counter.load() < key_num) {
    const int key = dist(thread_generator);
    const int value = value_dist(thread_generator);

    if (tree->Insert(key, value)) insert_success_counter.fetch_add(1);
    op_cnt++;
  }

  return op_cnt;
}

void skewedFindWorkload(uint32_t id, uint32_t key_num,
  test::BwTreeTestUtil::TreeType* tree)
{
  std::default_random_engine thread_generator(id);
  std::normal_distribution<double> dist(0, 0.01);
  
  while (insert_success_counter.load() < key_num) {
    int key = dist(thread_generator);

    tree->GetValue(key);
    total_read_counter++;
  }
}

void uniformFindWorkload(uint32_t id, uint32_t key_num,
  test::BwTreeTestUtil::TreeType* tree)
{
  std::default_random_engine thread_generator(id);
  std::uniform_int_distribution<int> dist(0, key_num);

  while (insert_success_counter.load() < key_num) {
    int key = dist(thread_generator);

    tree->GetValue(key);
    total_read_counter++;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 6)
  {
    std::cout << "usage: " << argv[0] << " <gc_interval> <writer ratio> <data size> <write skewed> <read skewed>" << std::endl;
    return -1;
  }

  const double gc_interval = std::stod(argv[1]);
  const double writer_ratio = std::atof(argv[2]);
  const int data_size = std::atoi(argv[3]);
  const bool write_skewed = std::atoi(argv[4]);
  const bool read_skewed = std::atoi(argv[5]);

  if (gc_interval > 0)
  {
    test::BwTreeTestUtil::TreeType::EpochManager::GC_CONTROLLER_ENABLE = false;
    test::BwTreeTestUtil::TreeType::EpochManager::GC_INTERVAL = std::atoi(argv[1]);
  }

  const uint32_t num_threads_ = 20;

  // This defines the key space (0 ~ (10M - 1))
  const uint32_t key_num = data_size * 1024 * 1024;

  common::WorkerPool thread_pool(num_threads_, {});
  thread_pool.Startup();
  auto *const tree = test::BwTreeTestUtil::GetEmptyTree();

  const int num_write_workers = num_threads_ * writer_ratio;
  const int num_read_workers = num_threads_ - num_write_workers;

  // Inserts in a 1M key space randomly until all keys has been inserted
  auto workload = [&](uint32_t id) {
    const uint32_t gcid = id + 1;
    tree->AssignGCID(gcid);

    uint32_t op_cnt = 0;
    
    if (id < num_write_workers)
    {
      if (write_skewed)
      {
        op_cnt = skewedInsertWorkload(id, key_num, tree);
      }
      else
      {
        op_cnt = uniformInsertWorkload(id, key_num, tree);
      }
    }
    else
    {
      if (read_skewed)
      {
        skewedFindWorkload(id, key_num, tree);
      }
      else
      {
        uniformFindWorkload(id, key_num, tree);
      }
    }
    
    tree->UnregisterThread(gcid);
    total_op_counter.fetch_add(op_cnt);
  };

  util::Timer<std::milli> timer;
  timer.Start();

  tree->UpdateThreadLocal(num_threads_ + 1);
  test::MultiThreadTestUtil::RunThreadsUntilFinish(&thread_pool, num_threads_, workload);
  tree->UpdateThreadLocal(1);

  timer.Stop();

  double ops = total_op_counter.load() / (timer.GetElapsed() / 1000.0);
  double success_ops = insert_success_counter.load() / (timer.GetElapsed() / 1000.0);
  std::cout << std::fixed << data_size << "M Insert(): " << timer.GetElapsed() << " (ms), "
    << "write throughput: " << ops << " (op/s), "
    << "successive write throughput: " << success_ops << " (op/s)" << std::endl;

  double read_ops = total_read_counter.load() / (timer.GetElapsed() / 1000.0);
  std::cout << std::fixed << "Get(): " << "read throughput: " << read_ops << " (op/s)" << std::endl;

  std::cout << std::endl;
  std::cout << "[per worker perf] writer: " << num_write_workers << ", reader: " << num_read_workers << std::endl;
  std::cout << "write throughput: " << ops / num_threads_ << " (op/s)" << std::endl;
  std::cout << "successive write throughput: " << success_ops / num_threads_ << " (op/s)" << std::endl;
  std::cout << "read throughput: " << read_ops / num_threads_ << " (op/s)" << std::endl;
  std::cout << "garbage length mu: " << test::BwTreeTestUtil::TreeType::EpochManager::GARBAGE_LENGTH_LATEST << std::endl;

  delete tree;

  return 0;
}
