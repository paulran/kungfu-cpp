#include <iostream>
#include <thread>
#include <chrono>
#include <kungfu/yijinjing/journal/journal.h>
#include <kungfu/longfist/longfist.h>

using namespace kungfu;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::journal;
using namespace kungfu::longfist;
using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;

int main(int argc, char *argv[]) {
  std::string data_dir = "/tmp/kf_example_data";
  if (argc > 1) {
    data_dir = argv[1];
  }

  auto locator = std::make_shared<kungfu::yijinjing::data::locator>(data_dir);
  auto location = kungfu::yijinjing::data::location::make_shared(mode::LIVE, category::STRATEGY, "demo", "producer", locator);

  reader_ptr r = std::make_shared<reader>(false);
  r->join(location, kungfu::yijinjing::data::location::PUBLIC, 0);

  std::cout << "Consumer started. Reading from: " << data_dir << std::endl;

  int count = 0;
  int timeout = 0;
  const int max_timeout = 100;

  while (timeout < max_timeout) {
    if (r->data_available()) {
      auto frame = r->current_frame();
      if (frame->msg_type() == Bar::tag) {
        auto bar = frame->data<Bar>();
        count++;
        if (count % 10 == 0) {
          std::cout << "Read bar " << count << ": "
                    << "instrument=" << bar.instrument_id.value << ", "
                    << "close=" << bar.close << ", "
                    << "volume=" << bar.volume << std::endl;
        }
      }
      r->next();
      timeout = 0;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      timeout++;
    }
  }

  std::cout << "Consumer finished. Total read: " << count << " bars" << std::endl;

  return 0;
}