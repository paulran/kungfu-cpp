import my_cpp_module

# 测试 Quote 类型
q = my_cpp_module.Quote()
print('Quote created successfully')
print(q.to_string())

# 测试设置字段
q.instrument_id = 'SHFE.cu2309'
q.exchange_id = 'SHFE'
q.last_price = 68000.0
q.volume = 1000
q.bid_price = [67990.0, 67980.0, 67970.0, 67960.0, 67950.0, 0, 0, 0, 0, 0]
q.bid_volume = [10, 20, 30, 40, 50, 0, 0, 0, 0, 0]
q.ask_price = [68010.0, 68020.0, 68030.0, 68040.0, 68050.0, 0, 0, 0, 0, 0]
q.ask_volume = [15, 25, 35, 45, 55, 0, 0, 0, 0, 0]
print('Quote after setting fields:')
print(q.to_string())

# 测试 Order 类型
o = my_cpp_module.Order()
print('Order created successfully')
print(o.to_string())

# 测试设置字段
o.order_id = 123456789
o.instrument_id = 'SHFE.cu2309'
o.side = my_cpp_module.Side.Buy
o.offset = my_cpp_module.Offset.Open
o.limit_price = 68000.0
o.volume = 10
print('Order after setting fields:')
print(o.to_string())

print('All tests passed!')