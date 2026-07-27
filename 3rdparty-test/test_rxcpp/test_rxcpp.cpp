#include <iostream>
#include <rxcpp/rx.hpp>

int main() {
    {// 使用 | 将操作符串联起来
        rxcpp::observable<>::range(1, 5)
            | rxcpp::operators::map([](int i) { return i * i; })   // 计算平方
            | rxcpp::operators::filter([](int i) { return i > 10; }) // 过滤
            | rxcpp::operators::subscribe<int>([](int i) {               // 订阅并输出
                std::cout << i << std::endl;                       // 输出: 16, 25
            });
    }

    {  // 创建一个发射 1, 2, 3 的 Observable
        auto values = rxcpp::observable<>::range(1, 3);

        // 订阅这个 Observable
        values.subscribe(
            [](int v) { std::cout << "OnNext: " << v << std::endl; }, // on_next 回调
            []() { std::cout << "OnCompleted" << std::endl; }        // on_completed 回调
        );
    }

    { // 从 vector 创建 Observable，然后进行链式操作
        std::vector<int> numbers = {1, 2, 3, 4, 5, 6};

        rxcpp::observable<>::iterate(numbers)
            .map([](int num) { return num * 2; })          // 将每个数字乘以 2
            .filter([](int num) { return num % 3 == 0; })  // 只保留能被 3 整除的
            .subscribe([](int num) { std::cout << num << " "; }); // 输出: 6 12

        std::cout << std::endl;

    }

    return 0;
}