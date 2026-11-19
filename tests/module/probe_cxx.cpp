// SPDX-License-Identifier: MPL-2.0
// C++ part of the feasibility probe: libc++, libc++abi and libunwind linked
// into a module or a guest, with no QNX C++ runtime involved.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "check.h"

namespace {

int global_ctor_value;
struct Global {
  Global() { global_ctor_value = 77; }
};
Global global_object;

struct Counted {
  static std::atomic<int> live;
  std::string name;
  explicit Counted(const char* n) : name(n) { ++live; }
  ~Counted() { --live; }
};
std::atomic<int> Counted::live{0};

thread_local Counted per_thread("unset");
thread_local int tl_value = 1;

struct Base {
  virtual ~Base() = default;
  virtual int kind() const { return 1; }
};
struct Derived : Base {
  int kind() const override { return 2; }
};

[[gnu::noinline]] void thrower(int depth) {
  if (depth == 0) throw std::runtime_error("deep");
  thrower(depth - 1);
}

int& function_static() {
  static int value = ++global_ctor_value;
  return value;
}

void check_exceptions() {
  bool caught = false;
  try {
    thrower(8);
  } catch (const std::exception& e) {
    caught = std::string(e.what()) == "deep";
  }
  bool rethrown = false;
  try {
    try {
      throw 42;
    } catch (...) {
      throw;
    }
  } catch (int v) {
    rethrown = v == 42;
  }
  std::exception_ptr ep;
  try {
    throw std::logic_error("ptr");
  } catch (...) {
    ep = std::current_exception();
  }
  bool via_ptr = false;
  try {
    std::rethrow_exception(ep);
  } catch (const std::logic_error&) {
    via_ptr = true;
  }
  bool out_of_memory = false;
  try {
    // A direct call: the compiler may drop an unused new-expression.
    void* volatile p = ::operator new(static_cast<size_t>(-1) / 2);
    ::operator delete(p);
  } catch (const std::bad_alloc&) {
    out_of_memory = true;
  }
  bool ok = caught && rethrown && via_ptr && out_of_memory;
  if (!ok)
    std::printf("  caught=%d rethrown=%d via_ptr=%d out_of_memory=%d\n", caught, rethrown, via_ptr, out_of_memory);
  check("c++: exceptions", ok);
}

void check_library() {
  std::unique_ptr<Base> b(new Derived);
  const Base& ref = *b;
  check("c++: rtti", dynamic_cast<const Derived*>(&ref) != nullptr && typeid(ref) == typeid(Derived) &&
                                ref.kind() == 2);

  std::map<std::string, int> m;
  std::unordered_map<int, std::string> um;
  std::vector<int> v;
  for (int i = 0; i < 1000; ++i) {
    m[std::to_string(i)] = i;
    um[i] = std::to_string(i * 2);
    v.push_back((i * 7919) % 1000);
  }
  std::sort(v.begin(), v.end());
  std::function<int(int)> f = [&](int x) { return m[std::to_string(x)] + 1; };
  auto sp = std::make_shared<std::string>("shared");
  std::weak_ptr<std::string> wp = sp;
  check("c++: containers", m.size() == 1000 && um[500] == "1000" && std::is_sorted(v.begin(), v.end()) &&
                                     v[0] == 0 && f(41) == 42 && wp.lock() && *wp.lock() == "shared");

  std::ostringstream os;
  os << std::hex << 255 << ' ' << std::dec << 3.5 << ' ' << std::string("x");
  std::istringstream is("12 34.5 word");
  int a = 0;
  double d = 0;
  std::string w;
  is >> a >> d >> w;
  check("c++: string streams", os.str() == "ff 3.5 x" && a == 12 && d == 34.5 && w == "word");
  std::cout << "c++: std::cout works" << std::endl;

  check("c++: function statics", function_static() == 78 && function_static() == 78);
}

void check_threads() {
  std::mutex mu;
  std::condition_variable cv;
  int ready = 0;
  std::atomic<long> sum{0};
  int live_before = Counted::live.load();
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&, i] {
      tl_value = 100 + i;
      per_thread.name = "t" + std::to_string(i);
      {
        std::lock_guard<std::mutex> lock(mu);
        ++ready;
      }
      cv.notify_all();
      for (int k = 0; k < 10000; ++k) sum += 1;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      if (tl_value != 100 + i || per_thread.name != "t" + std::to_string(i)) sum += 1000000;
    });
  }
  {
    std::unique_lock<std::mutex> lock(mu);
    cv.wait(lock, [&] { return ready == 4; });
  }
  for (auto& t : threads) t.join();
  check("c++: std::thread, mutex, cv", sum == 40000);
  check("c++: thread_local ctor/dtor", Counted::live.load() == live_before && tl_value == 1);

  auto t0 = std::chrono::steady_clock::now();
  int64_t h0 = nbt_host->monotonic_ns();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  int64_t h1 = nbt_host->monotonic_ns();
  auto t1 = std::chrono::steady_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  if (ms < 15 || ms >= 2000)
    std::printf("  steady_clock %lld -> %lld ns, host clock slept %lld ms\n",
                static_cast<long long>(t0.time_since_epoch().count()),
                static_cast<long long>(t1.time_since_epoch().count()), static_cast<long long>((h1 - h0) / 1000000));
  check("c++: steady_clock sleep", ms >= 15 && ms < 2000);
}

}  // namespace

extern "C" void probe_cxx(void) {
  check("c++: static constructors", global_ctor_value == 77);
  check_exceptions();
  check_library();
  check_threads();
}
