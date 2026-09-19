/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
#include <windows.h>
#include <ndk/rtlfuncs.h>
#include <exception>
#include <typeinfo>
#include <new>
#include <stdexcept>
#include <vector>
#include <string>

static unsigned Checks, Failures;
#define CHECK(e) do { ++Checks; if (!(e)) { ++Failures; \
    DbgPrint("RISCVCXXTEST: FAIL line=%u\n", __LINE__); } } while (0)

struct Base { virtual ~Base() {} };
struct Derived : Base { int Value; Derived() : Value(42) {} };
struct Other : Base {};
struct Cleanup { int &Count; Cleanup(int &n) : Count(n) {} ~Cleanup() { ++Count; } };

static void AllocationFailure() { throw 57; }

extern "C" int RunCxxTests(void)
{
    int Destroyed = 0;
    bool Caught = false;
    DbgPrint("RISCVCXXTEST: BEGIN\n");
    try { Cleanup Local(Destroyed); throw 42; }
    catch (int Value) { CHECK(Value == 42); Caught = true; }
    CHECK(Caught && Destroyed == 1);
    Derived Object;
    Base *Pointer = &Object;
    CHECK(dynamic_cast<Derived *>(Pointer) == &Object);
    CHECK(dynamic_cast<Other *>(Pointer) == NULL);
    CHECK(typeid(*Pointer) == typeid(Derived));
    CHECK(typeid(*Pointer) != typeid(Base));
    CHECK(typeid(*Pointer).hash_code() == typeid(Derived).hash_code());
    Caught = false;
    try { (void)dynamic_cast<Other &>(*Pointer); }
    catch (const std::bad_cast &Error) { CHECK(Error.what() != NULL); Caught = true; }
    CHECK(Caught);
    Pointer = NULL;
    Caught = false;
    try { (void)typeid(*Pointer); }
    catch (const std::bad_typeid &Error) { CHECK(Error.what() != NULL); Caught = true; }
    CHECK(Caught);
    std::vector<std::string> Strings;
    Strings.push_back("riscv64");
    CHECK(Strings.at(0) == "riscv64");
    Caught = false;
    try { (void)Strings.at(1); }
    catch (const std::exception &Error) { CHECK(Error.what()[0] != 0); Caught = true; }
    CHECK(Caught);
    std::new_handler Previous = std::set_new_handler(AllocationFailure);
    Caught = false;
    try { void *p = ::operator new((size_t)-1); ::operator delete(p); }
    catch (int Value) { CHECK(Value == 57); Caught = true; }
    CHECK(Caught);
    std::set_new_handler(NULL);
    Caught = false;
    try { void *p = ::operator new((size_t)-1); ::operator delete(p); }
    catch (const std::bad_alloc &) { Caught = true; }
    CHECK(Caught);
    CHECK(::operator new((size_t)-1, std::nothrow) == NULL);
    std::set_new_handler(Previous);
    DbgPrint("RISCVCXXTEST: END checks=%u failures=%u\n", Checks, Failures);
    return Failures;
}
