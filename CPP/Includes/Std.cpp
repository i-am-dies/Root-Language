#pragma once

#include <algorithm>
#include <any>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using namespace std;

// ----------------------------------------------------------------

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using u128 = __uint128_t;
using usize = size_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using i128 = __int128_t;
using isize = ptrdiff_t;

using f32 = float;
using f64 = double;
using f128 = long double;

template <typename T>
using sp = shared_ptr<T>;

template <typename T>
using up = unique_ptr<T>;

template <typename T>
using wp = weak_ptr<T>;

// ----------------------------------------------------------------

template <typename T>
class recursive_wrapper {
private:
	up<T> pointer;

public:
	recursive_wrapper() : pointer(make_unique<T>()) {}
	recursive_wrapper(const T& value) : pointer(make_unique<T>(value)) {}
	recursive_wrapper(T&& value) : pointer(make_unique<T>(move(value))) {}
	recursive_wrapper(const recursive_wrapper& other) : pointer(make_unique<T>(*other.pointer)) {}
	recursive_wrapper(recursive_wrapper&& other) noexcept = default;

	recursive_wrapper& operator=(const recursive_wrapper& other) {
		if(this != &other) {
			pointer = make_unique<T>(*other.pointer);
		}

		return *this;
	}

	recursive_wrapper& operator=(recursive_wrapper&& other) noexcept = default;

	operator T&() const { return *pointer; }

	T& get() { return *pointer; }
	T& operator*() { return *pointer; }
	T* operator->() { return pointer.get(); }

	const T& get() const { return *pointer; }
	const T& operator*() const { return *pointer; }
	const T* operator->() const { return pointer.get(); }

	bool operator<(const recursive_wrapper& other) const { return *pointer < *other.pointer; }
	bool operator>(const recursive_wrapper& other) const { return *pointer > *other.pointer; }
	bool operator<=(const recursive_wrapper& other) const { return *pointer <= *other.pointer; }
	bool operator>=(const recursive_wrapper& other) const { return *pointer >= *other.pointer; }
	bool operator==(const recursive_wrapper& other) const { return *pointer == *other.pointer; }
	bool operator!=(const recursive_wrapper& other) const { return *pointer != *other.pointer; }
};

// ----------------------------------------------------------------

template <typename Container, typename Predicate>
bool some(const Container& container, Predicate predicate) {
	return any_of(container.begin(), container.end(), predicate);
}

template <typename Container, typename T>
bool contains(const Container& container, const T& value) {
	return find(container.begin(), container.end(), value) != container.end();
}

template <typename Container, typename T>
bool count(const Container& container, const T& value) {
	return count(container.begin(), container.end(), value);
}

template <typename Container, typename UnaryPredicate>
Container filter(const Container& container, UnaryPredicate predicate) {
	Container result;

	copy_if(container.begin(), container.end(), back_inserter(result), predicate);

	return result;
}

template <typename Container, typename UnaryPredicate>
Container transform(const Container& container, UnaryPredicate predicate) {
	Container result;

	result.reserve(container.size());
	transform(container.begin(), container.end(), back_inserter(result), predicate);

	return result;
}

template <typename Container, typename T>
int index_of(const Container& container, const T& value) {
	auto it = find(container.begin(), container.end(), value);

	return it != container.end() ? it-container.begin() : -1;
}

template <typename Container, typename Predicate>
int find_index(const Container& container, Predicate predicate) {
	auto it = find_if(container.begin(), container.end(), predicate);

	return it != container.end() ? it-container.begin() : -1;
}

template <typename Container>
Container concat(const Container& LHS, const Container& RHS) {
	Container result(LHS);

	result.insert(RHS.begin(), RHS.end());

	return result;
}

template <typename Container>
string join(const Container& container, const string& separator = ", ") {
	string result;
	auto it = container.begin();

	while(it != container.end()) {
		result += *it;

		if(next(it) != container.end()) {
			result += separator;
		}

		it++;
	}

	return result;
}

template <typename Map>
typename Map::mapped_type* find_ptr(Map& map, const typename Map::key_type& key) {
	auto it = map.find(key);

	if(it != map.end()) {
		return &it->second;
	}

	return nullptr;
}

template <typename T>
sp<decay_t<T>> SP(T&& value) {
	return make_shared<decay_t<T>>(forward<T>(value));
}

template <typename T, typename... Args>
sp<T> SP(Args&&... args) {
	return make_shared<T>(forward<Args>(args)...);
}

static string tolower(string_view string) {
	auto result = ::string(string);

	transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return tolower(c); });

	return result;
}

template <typename T>
optional<T> any_optcast(const any& value) {
	if(const optional<T>* v = any_cast<optional<T>>(&value)) {
		return *v;
	} else
	if(const T* v = any_cast<T>(&value)) {
		return optional<T>(*v);
	}

	return nullopt;
}

template <typename T>
sp<T> any_spcast(const any& value) {
	if(const sp<T>* v = any_cast<sp<T>>(&value)) {
		return *v;
	} else
	if(const T* v = any_cast<T>(&value)) {
		return make_shared<T>(*v);
	}

	return nullptr;
}

optional<string> read_file(const filesystem::path& path) {
	if(!filesystem::exists(path) || !filesystem::is_regular_file(path)) {
		return nullopt;
	}

	ifstream file(path, ios::in);

	if(!file.is_open()) {
		return nullopt;
	}

	ostringstream ss;

	ss << file.rdbuf();

	if(file.fail()) {
		return nullopt;
	}

	return ss.str();
}

template<typename... Args>
void println(Args&&... args) {
	static mutex coutMutex;
	ostringstream oss;
	(oss << ... << args) << '\n';
	lock_guard<mutex> lock(coutMutex);

	cout << oss.str();
}

string repeat(const string& s, usize times) {
    string result;

	result.reserve(s.size()*times);

	for(usize i = 0; i < times; ++i) {
        result += s;
    }

	return result;
}