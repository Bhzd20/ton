/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#include "td/utils/MpmcWaiter.h"
#include "td/utils/port/sleep.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <atomic>

#if !TD_THREAD_UNSUPPORTED
template <class W>
void test_waiter_stress_one_one() {
  td::Stage run;
  td::Stage check;

  std::vector<td::thread> threads;
  std::atomic<size_t> value{0};
  size_t write_cnt = 10;
  td::unique_ptr<W> waiter;
  size_t threads_n = 2;
  for (size_t i = 0; i < threads_n; i++) {
    threads.push_back(td::thread([&, id = static_cast<td::uint32>(i)] {
      for (td::uint64 round = 1; round < 100000; round++) {
        if (id == 0) {
          value = 0;
          waiter = td::make_unique<W>();
          write_cnt = td::Random::fast(1, 10);
        }
        run.wait(round * threads_n);
        if (id == 1) {
          for (size_t i = 0; i < write_cnt; i++) {
            value.store(i + 1, std::memory_order_relaxed);
            waiter->notify();
          }
        } else {
          typename W::Slot slot;
          waiter->init_slot(slot, id);
          for (size_t i = 1; i <= write_cnt; i++) {
            while (true) {
              auto x = value.load(std::memory_order_relaxed);
              if (x >= i) {
                break;
              }
              waiter->wait(slot);
            }
            waiter->stop_wait(slot);
          }
          waiter->stop_wait(slot);
        }
        check.wait(round * threads_n);
      }
    }));
  }
  for (auto &thread : threads) {
    thread.join();
  }
}
TEST(MpmcEagerWaiter, stress_one_one) {
  test_waiter_stress_one_one<td::MpmcEagerWaiter>();
}
// TEST(MpmcSleepyWaiter, stress_one_one) {
//   test_waiter_stress_one_one<td::MpmcSleepyWaiter>();
// }

template <class W>
void test_waiter_stress() {
  td::Stage run;
  td::Stage check;

  std::vector<td::thread> threads;
  size_t write_n;
  size_t read_n;
  std::atomic<size_t> write_pos{0};
  std::atomic<size_t> read_pos{0};
  size_t end_pos;
  size_t write_cnt;
  size_t threads_n = 20;
  td::unique_ptr<W> waiter;
  for (size_t i = 0; i < threads_n; i++) {
    threads.push_back(td::thread([&, id = static_cast<td::uint32>(i)] {
      for (td::uint64 round = 1; round < 1000; round++) {
        if (id == 0) {
          write_n = td::Random::fast(1, 10);
          read_n = td::Random::fast(1, 10);
          write_cnt = td::Random::fast(1, 50);
          end_pos = write_n * write_cnt;
          write_pos = 0;
          read_pos = 0;
          waiter = td::make_unique<W>();
        }
        run.wait(round * threads_n);
        if (id <= write_n) {
          for (size_t i = 0; i < write_cnt; i++) {
            if (td::Random::fast(0, 20) == 0) {
              td::usleep_for(td::Random::fast(1, 300));
            }
            write_pos.fetch_add(1, std::memory_order_relaxed);
            waiter->notify();
          }
        } else if (id > 10 && id - 10 <= read_n) {
          typename W::Slot slot;
          waiter->init_slot(slot, id);
          while (true) {
            auto x = read_pos.load(std::memory_order_relaxed);
            if (x == end_pos) {
              waiter->stop_wait(slot);
              break;
            }
            if (x == write_pos.load(std::memory_order_relaxed)) {
              waiter->wait(slot);
              continue;
            }
            waiter->stop_wait(slot);
            read_pos.compare_exchange_strong(x, x + 1, std::memory_order_relaxed);
          }
        }
        check.wait(round * threads_n);
        if (id == 0) {
          waiter->close();
        }
      }
    }));
  }
  for (auto &thread : threads) {
    thread.join();
  }
}
TEST(MpmcEagerWaiter, stress_multi) {
  test_waiter_stress<td::MpmcEagerWaiter>();
}
TEST(MpmcSleepyWaiter, stress_multi) {
  test_waiter_stress<td::MpmcSleepyWaiter>();
}
#endif  // !TD_THREAD_UNSUPPORTED
