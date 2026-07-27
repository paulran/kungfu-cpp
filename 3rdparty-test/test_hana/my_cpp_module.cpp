#include "types.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstring>

namespace py = pybind11;
namespace hana = boost::hana;

// 定义 pybind11 type caster 来自动处理 kfarray<char, Size> <-> Python str
namespace pybind11::detail {
template <size_t Size> struct type_caster<kfarray<char, Size>> {
  using ArrayType = kfarray<char, Size>;
  using value_conv = make_caster<char>;

  bool load(handle src, bool convert) {
    if (!isinstance<str>(src))
      return false;
    std::string &&s = reinterpret_borrow<str>(src);
    if (s.length() > Size)
      return false;
    strcpy(value.value, s.c_str());
    return true;
  }

  template <typename T> static handle cast(T &&src, return_value_policy policy, handle parent) {
    return str(src.value).release();
  }

  PYBIND11_TYPE_CASTER(ArrayType, _("String[") + value_conv::name + _("[") + _<Size>() + _("]") + _("]"));
};

// 定义 pybind11 type caster 来自动处理 kfarray<ValueType, Size> <-> Python list
template <typename ValueType, size_t Size> struct type_caster<kfarray<ValueType, Size>> {
  using ArrayType = kfarray<ValueType, Size>;
  using value_conv = make_caster<ValueType>;

  bool load(handle src, bool convert) {
    if (!isinstance<sequence>(src))
      return false;
    auto l = reinterpret_borrow<sequence>(src);
    if (l.size() > Size)
      return false;
    size_t ctr = 0;
    for (auto it : l) {
      value_conv conv;
      if (!conv.load(it, convert))
        return false;
      value.value[ctr++] = cast_op<ValueType &&>(std::move(conv));
    }
    return true;
  }

  template <typename T> static handle cast(T &&src, return_value_policy policy, handle parent) {
    list l(src.size());
    for (size_t index = 0; index < Size; index++) {
      auto &&value = src.value[index];
      auto value_ = reinterpret_steal<object>(value_conv::cast(forward_like<T>(value), policy, parent));
      PyList_SET_ITEM(l.ptr(), (ssize_t)index, value_.release().ptr());
    }
    return l.release();
  }

  PYBIND11_TYPE_CASTER(ArrayType, _("List[") + value_conv::name + _("[") + _<Size>() + _("]") + _("]"));
};
} // namespace pybind11::detail

// 参考 bind_data_type 的实现，使用 hana 自动绑定数据类型
template <typename DataType>
void bind_data_type(py::module &m, const char *type_name) {
  // 创建 py::class_ 对象
  py::class_<DataType> py_class(m, type_name);
  py_class.def(py::init<>());
  py_class.def(py::init<const std::string &>());

  // 使用 hana::for_each 自动遍历所有字段并绑定
  hana::for_each(hana::accessors<DataType>(), [&](auto it) {
    auto name = hana::first(it);
    auto accessor = hana::second(it);
    py_class.def_readwrite(name.c_str(), member_pointer_trait<decltype(accessor)>().pointer());
  });

  // 绑定静态属性和方法
  py_class.def_readonly_static("__tag__", &DataType::tag);
  py_class.def_readonly_static("__has_data__", &DataType::has_data);

  py_class.def("to_string", &DataType::to_string);
  py_class.def("__repr__", &DataType::to_string);
  py_class.def("__sizeof__", [](const DataType &target) { return sizeof(target); });
  py_class.def("__parse__", [](DataType &target, std::string &s) { target.parse(s.c_str(), s.length()); });
}

PYBIND11_MODULE(my_cpp_module, m) {
    m.doc() = "Quantitative trading data types module";
    
    // 绑定枚举类型
    py::enum_<InstrumentType>(m, "InstrumentType")
        .value("Unknown", InstrumentType::Unknown)
        .value("Stock", InstrumentType::Stock)
        .value("Future", InstrumentType::Future)
        .value("Bond", InstrumentType::Bond)
        .value("StockOption", InstrumentType::StockOption)
        .value("TechStock", InstrumentType::TechStock)
        .value("Fund", InstrumentType::Fund)
        .value("Index", InstrumentType::Index)
        .value("Repo", InstrumentType::Repo)
        .value("Warrant", InstrumentType::Warrant)
        .value("Iopt", InstrumentType::Iopt)
        .value("Crypto", InstrumentType::Crypto)
        .export_values();
    
    py::enum_<ExecType>(m, "ExecType")
        .value("Unknown", ExecType::Unknown)
        .value("Cancel", ExecType::Cancel)
        .value("Trade", ExecType::Trade)
        .export_values();
    
    py::enum_<BsFlag>(m, "BsFlag")
        .value("Unknown", BsFlag::Unknown)
        .value("Buy", BsFlag::Buy)
        .value("Sell", BsFlag::Sell)
        .export_values();
    
    py::enum_<Side>(m, "Side")
        .value("Buy", Side::Buy)
        .value("Sell", Side::Sell)
        .value("Lock", Side::Lock)
        .value("Unlock", Side::Unlock)
        .value("Exec", Side::Exec)
        .value("Drop", Side::Drop)
        .value("Purchase", Side::Purchase)
        .value("Redemption", Side::Redemption)
        .value("Split", Side::Split)
        .value("Merge", Side::Merge)
        .value("MarginTrade", Side::MarginTrade)
        .value("ShortSell", Side::ShortSell)
        .value("RepayMargin", Side::RepayMargin)
        .value("RepayStock", Side::RepayStock)
        .value("CashRepayMargin", Side::CashRepayMargin)
        .value("StockRepayStock", Side::StockRepayStock)
        .value("SurplusStockTransfer", Side::SurplusStockTransfer)
        .value("GuaranteeStockTransferIn", Side::GuaranteeStockTransferIn)
        .value("GuaranteeStockTransferOut", Side::GuaranteeStockTransferOut)
        .value("Unknown", Side::Unknown)
        .export_values();
    
    py::enum_<Offset>(m, "Offset")
        .value("Open", Offset::Open)
        .value("Close", Offset::Close)
        .value("CloseToday", Offset::CloseToday)
        .value("CloseYesterday", Offset::CloseYesterday)
        .export_values();
    
    py::enum_<HedgeFlag>(m, "HedgeFlag")
        .value("Speculation", HedgeFlag::Speculation)
        .value("Arbitrage", HedgeFlag::Arbitrage)
        .value("Hedge", HedgeFlag::Hedge)
        .value("Covered", HedgeFlag::Covered)
        .export_values();
    
    py::enum_<PriceType>(m, "PriceType")
        .value("Limit", PriceType::Limit)
        .value("Any", PriceType::Any)
        .value("FakBest5", PriceType::FakBest5)
        .value("ForwardBest", PriceType::ForwardBest)
        .value("ReverseBest", PriceType::ReverseBest)
        .value("Fak", PriceType::Fak)
        .value("Fok", PriceType::Fok)
        .value("Unknown", PriceType::Unknown)
        .export_values();
    
    py::enum_<VolumeCondition>(m, "VolumeCondition")
        .value("Any", VolumeCondition::Any)
        .value("Min", VolumeCondition::Min)
        .value("All", VolumeCondition::All)
        .export_values();
    
    py::enum_<TimeCondition>(m, "TimeCondition")
        .value("IOC", TimeCondition::IOC)
        .value("GFD", TimeCondition::GFD)
        .value("GTC", TimeCondition::GTC)
        .export_values();
    
    py::enum_<OrderStatus>(m, "OrderStatus")
        .value("Unknown", OrderStatus::Unknown)
        .value("Submitted", OrderStatus::Submitted)
        .value("Pending", OrderStatus::Pending)
        .value("Cancelled", OrderStatus::Cancelled)
        .value("Error", OrderStatus::Error)
        .value("Filled", OrderStatus::Filled)
        .value("PartialFilledNotActive", OrderStatus::PartialFilledNotActive)
        .value("PartialFilledActive", OrderStatus::PartialFilledActive)
        .value("Lost", OrderStatus::Lost)
        .export_values();
    
    // 使用 bind_data_type 自动绑定 Quote 和 Order 类型
    bind_data_type<Quote>(m, "Quote");
    bind_data_type<Order>(m, "Order");
}
