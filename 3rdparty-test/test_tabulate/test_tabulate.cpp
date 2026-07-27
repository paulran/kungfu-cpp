#include <tabulate/table.hpp>
#include <iostream>
using namespace tabulate;

int main() {
    Table table;

    table.add_row({"项目", "状态", "负责人"});
    table.add_row({"登录模块", "已完成", "Alice"});
    table.add_row({"支付网关", "进行中", "Bob"});
    table.add_row({"日志系统", "待开始", "Charlie"});

    // 为特定单元格设置样式
    // 将 "已完成" 设置为绿色，并加粗
    table[1][1].format()
        .font_color(Color::green)
        .font_style({FontStyle::bold});

    // 将 "进行中" 设置为黄色，并加斜体
    table[2][1].format()
        .font_color(Color::yellow)
        .font_style({FontStyle::italic});

    // 将 "待开始" 设置为红色
    table[3][1].format()
        .font_color(Color::red);

    std::cout << table << std::endl;
    return 0;
}