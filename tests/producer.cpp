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

class null_publisher : public yijinjing::publisher {
public:
  bool is_usable() override { return true; }
  void setup() override {}
  int notify() override { return 0; }
  int publish(const std::string &, int) override { return 0; }
};

int main(int argc, char *argv[]) {
  std::string data_dir = "/tmp/kf_example_data";
  if (argc > 1) {
    data_dir = argv[1];
  }

  auto locator = std::make_shared<kungfu::yijinjing::data::locator>(data_dir);
  auto location = kungfu::yijinjing::data::location::make_shared(mode::LIVE, category::STRATEGY, "demo", "producer", locator);
  auto publisher = std::make_shared<null_publisher>();

  writer_ptr w = std::make_shared<writer>(location, kungfu::yijinjing::data::location::PUBLIC, false, publisher);

  std::cout << "Producer started. Location: " << location->uname << std::endl;
  std::cout << "Writing data to: " << data_dir << std::endl;

  for (int i = 0; i < 100; ++i) {
    Bar bar{};
    snprintf(bar.instrument_id.value, bar.instrument_id.length, "%s", "600000.SH");
    snprintf(bar.exchange_id.value, bar.exchange_id.length, "%s", "SHFE");
    bar.instrument_type = InstrumentType::Stock;
    bar.open = 10.0 + i * 0.1;
    bar.high = bar.open + 0.5;
    bar.low = bar.open - 0.5;
    bar.close = bar.open + 0.2;
    bar.volume = 10000 + i * 100;
    bar.start_time = time::now_in_nano();
    bar.end_time = bar.start_time + 60000000000LL;

    w->write(time::now_in_nano(), bar);

    if (i % 10 == 0) {
      std::cout << "Written " << (i + 1) << " bars, close: " << bar.close << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cout << "Producer finished. Total written: 100 bars" << std::endl;

  return 0;
}