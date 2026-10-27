#include "hooks.h"

#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace nm;

static int failed = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failed; } } while (0)

static Json arithmeticScript(const char* op, int64_t a, int64_t b) {
    Json expr = Json::object();
    expr["op"] = op; expr["a"] = a; expr["b"] = b;
    Json action = Json::object();
    action["do"] = "set_result"; action["key"] = "value"; action["value"] = expr;
    Json script = Json::object();
    script["handlers"]["arithmetic"] = Json::array();
    script["handlers"]["arithmetic"].push(action);
    return script;
}

int main() {
    HostApi host;
    for (double value : {1e100, -1e100, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(), 9223372036854775808.0}) {
        bool rejected = false;
        try { (void)Json(value).asInt(); } catch (const JsonError&) { rejected = true; }
        CHECK(rejected);
    }
    CHECK(Json(-9223372036854775808.0).asInt() == std::numeric_limits<int64_t>::min());
    CHECK(Json(17.5).asInt() == 17);

    const int64_t lo = std::numeric_limits<int64_t>::min(), hi = std::numeric_limits<int64_t>::max();
    struct ArithmeticCase { const char* op; int64_t a, b; };
    for (const auto& c : {ArithmeticCase{"+", hi, 1}, {"+", lo, -1}, {"-", lo, 1},
                          {"-", hi, -1}, {"*", hi, 2}, {"*", lo, -1}, {"*", -2, lo},
                          {"/", lo, -1}, {"%", lo, -1}}) {
        ActionEngine engine;
        CHECK(engine.load("math", arithmeticScript(c.op, c.a, c.b), {}).empty());
        HookCtx ctx;
        std::string error;
        CHECK(!engine.run("math:arithmetic", ctx, host, nullptr, &error));
        CHECK(error.find("overflow") != std::string::npos);
    }
    {
        ActionEngine engine;
        CHECK(engine.load("math", arithmeticScript("*", lo, 1), {}).empty());
        HookCtx ctx;
        std::string error;
        CHECK(engine.run("math:arithmetic", ctx, host, nullptr, &error));
        CHECK(ctx.result["value"].asInt() == lo);
    }
    {
        ActionEngine engine;
        CHECK(engine.load("math", Json::parse(R"({"handlers":{"huge":[{"do":"set_var","name":"x","value":{"op":"+","a":1e100,"b":1}}]}})"), {}).empty());
        HookCtx ctx;
        std::string error;
        CHECK(!engine.run("math:huge", ctx, host, nullptr, &error));
        CHECK(error.find("int64") != std::string::npos);
    }
    {
        ActionEngine engine;
        HookBus bus;
        CHECK(engine.load("loop", Json::parse(R"({"handlers":{
          "loop":[{"do":"emit","hook":"day.start"},{"do":"set_result","key":"escaped","value":true}],
          "ok":[{"do":"set_result","key":"healthy","value":true}]}})"), {}).empty());
        int calls = 0;
        bus.bind("loop", "day.start", "", 0, [&](HookCtx& ctx) {
            ++calls;
            std::string error;
            if (!engine.run("loop:loop", ctx, host, nullptr, &error)) throw std::runtime_error(error);
        });
        CHECK(engine.load("healthy", Json::parse(R"({"handlers":{"ok":[{"do":"set_result","key":"healthy","value":true}]}})"), {}).empty());
        bus.bind("healthy", "day.start", "", 1, [&](HookCtx& ctx) {
            std::string error;
            if (!engine.run("healthy:ok", ctx, host, nullptr, &error)) throw std::runtime_error(error);
        });
        engine.emitFn = [&](HookCtx& ctx) { bus.fire(ctx); };
        HookCtx ctx;
        ctx.hook = "day.start";
        CHECK(bus.fire(ctx).faults > 0);
        CHECK(calls <= engine.limits.maxCallDepth + 1);
        CHECK(!ctx.result.has("escaped"));
        CHECK(!bus.log().empty());
        CHECK(bus.faults().count("healthy") == 0);
        CHECK(ctx.result["healthy"].asBool());
        std::string error;
        CHECK(engine.run("loop:ok", ctx, host, nullptr, &error));
        CHECK(ctx.result["healthy"].asBool());
    }
    {
        ActionEngine engine;
        HookBus bus;
        engine.limits.maxSteps = 30;
        CHECK(engine.load("budget", Json::parse(R"({"handlers":{
          "outer":[{"do":"repeat","times":20,"do_list":[{"do":"emit","hook":"day.end"}]},
                   {"do":"set_result","key":"escaped","value":true}],
          "inner":[{"do":"set_var","name":"x","value":1}]}})"), {}).empty());
        int emitted = 0;
        bus.bind("budget", "day.end", "", 0, [&](HookCtx& ctx) {
            ++emitted;
            std::string error;
            if (!engine.run("budget:inner", ctx, host, nullptr, &error)) throw std::runtime_error(error);
        });
        engine.emitFn = [&](HookCtx& ctx) { bus.fire(ctx); };
        HookCtx ctx;
        std::string error;
        CHECK(!engine.run("budget:outer", ctx, host, nullptr, &error));
        CHECK(error.find("anggaran") != std::string::npos);
        CHECK(emitted < 20);
        CHECK(!ctx.result.has("escaped"));
    }
    std::printf("script boundary tests: %d failure(s)\n", failed);
    return failed ? 1 : 0;
}
