test_hana:
	g++ -g -o test_hana.out test_hana.cpp -I. -I../../3rdparty/hana/include -I../../3rdparty/nlohmann/single_include
my_cpp_module:
	g++ -g -Wall -shared -fPIC -o my_cpp_module.so my_cpp_module.cpp `~/.pyenv/shims/python3 -m pybind11 --includes` -I. -I../../3rdparty/hana/include -I../../3rdparty/nlohmann/single_include

clean:
	rm -f test_hana.out my_cpp_module.so

.PHONY: test_hana my_cpp_module