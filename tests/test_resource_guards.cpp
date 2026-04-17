#include "catch2.hpp"
#include "../src/core/ofxGgmlResourceGuards.h"

// Note: These tests verify the guard classes work correctly without
// actually allocating real ggml resources (which would require backend setup).
// We focus on testing the RAII semantics, move operations, and safety.

TEST_CASE("GgmlBackendGuard default construction", "[resource_guards]") {
	GgmlBackendGuard guard;

	SECTION("Default constructed guard is null") {
		REQUIRE(guard.get() == nullptr);
		REQUIRE_FALSE(static_cast<bool>(guard));
	}
}

TEST_CASE("GgmlBackendGuard explicit construction", "[resource_guards]") {
	SECTION("Construct with nullptr") {
		GgmlBackendGuard guard(nullptr);
		REQUIRE(guard.get() == nullptr);
		REQUIRE_FALSE(static_cast<bool>(guard));
	}

	SECTION("Boolean conversion") {
		GgmlBackendGuard nullGuard(nullptr);
		REQUIRE_FALSE(static_cast<bool>(nullGuard));
	}
}

TEST_CASE("GgmlBackendGuard move semantics", "[resource_guards]") {
	SECTION("Move constructor") {
		GgmlBackendGuard guard1(nullptr);
		GgmlBackendGuard guard2(std::move(guard1));

		REQUIRE(guard1.get() == nullptr);
		REQUIRE(guard2.get() == nullptr);
	}

	SECTION("Move assignment") {
		GgmlBackendGuard guard1(nullptr);
		GgmlBackendGuard guard2;

		guard2 = std::move(guard1);

		REQUIRE(guard1.get() == nullptr);
		REQUIRE(guard2.get() == nullptr);
	}

	SECTION("Self-move assignment is safe") {
		GgmlBackendGuard guard(nullptr);
		guard = std::move(guard);
		REQUIRE(guard.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendGuard reset", "[resource_guards]") {
	SECTION("Reset to nullptr") {
		GgmlBackendGuard guard(nullptr);
		guard.reset();
		REQUIRE(guard.get() == nullptr);
	}

	SECTION("Reset with new value") {
		GgmlBackendGuard guard;
		guard.reset(nullptr);
		REQUIRE(guard.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendGuard release", "[resource_guards]") {
	SECTION("Release ownership") {
		GgmlBackendGuard guard(nullptr);
		auto ptr = guard.release();
		REQUIRE(ptr == nullptr);
		REQUIRE(guard.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendBufferGuard default construction", "[resource_guards]") {
	GgmlBackendBufferGuard guard;

	SECTION("Default constructed guard is null") {
		REQUIRE(guard.get() == nullptr);
		REQUIRE_FALSE(static_cast<bool>(guard));
	}
}

TEST_CASE("GgmlBackendBufferGuard move semantics", "[resource_guards]") {
	SECTION("Move constructor") {
		GgmlBackendBufferGuard guard1(nullptr);
		GgmlBackendBufferGuard guard2(std::move(guard1));

		REQUIRE(guard1.get() == nullptr);
		REQUIRE(guard2.get() == nullptr);
	}

	SECTION("Move assignment") {
		GgmlBackendBufferGuard guard1(nullptr);
		GgmlBackendBufferGuard guard2;

		guard2 = std::move(guard1);

		REQUIRE(guard1.get() == nullptr);
		REQUIRE(guard2.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendBufferGuard reset", "[resource_guards]") {
	SECTION("Reset to nullptr") {
		GgmlBackendBufferGuard guard(nullptr);
		guard.reset();
		REQUIRE(guard.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendBufferGuard release", "[resource_guards]") {
	SECTION("Release ownership") {
		GgmlBackendBufferGuard guard(nullptr);
		auto ptr = guard.release();
		REQUIRE(ptr == nullptr);
		REQUIRE(guard.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendSchedGuard default construction", "[resource_guards]") {
	GgmlBackendSchedGuard guard;

	SECTION("Default constructed guard is null") {
		REQUIRE(guard.get() == nullptr);
		REQUIRE_FALSE(static_cast<bool>(guard));
	}
}

TEST_CASE("GgmlBackendSchedGuard move semantics", "[resource_guards]") {
	SECTION("Move constructor") {
		GgmlBackendSchedGuard guard1(nullptr);
		GgmlBackendSchedGuard guard2(std::move(guard1));

		REQUIRE(guard1.get() == nullptr);
		REQUIRE(guard2.get() == nullptr);
	}

	SECTION("Move assignment") {
		GgmlBackendSchedGuard guard1(nullptr);
		GgmlBackendSchedGuard guard2;

		guard2 = std::move(guard1);

		REQUIRE(guard1.get() == nullptr);
		REQUIRE(guard2.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendSchedGuard reset", "[resource_guards]") {
	SECTION("Reset to nullptr") {
		GgmlBackendSchedGuard guard(nullptr);
		guard.reset();
		REQUIRE(guard.get() == nullptr);
	}
}

TEST_CASE("GgmlBackendSchedGuard release", "[resource_guards]") {
	SECTION("Release ownership") {
		GgmlBackendSchedGuard guard(nullptr);
		auto ptr = guard.release();
		REQUIRE(ptr == nullptr);
		REQUIRE(guard.get() == nullptr);
	}
}

TEST_CASE("Guard copy operations are deleted", "[resource_guards]") {
	// These should not compile if attempted:
	// GgmlBackendGuard g1;
	// GgmlBackendGuard g2 = g1; // Error: copy constructor deleted
	// g2 = g1; // Error: copy assignment deleted

	SECTION("Guards enforce move-only semantics") {
		REQUIRE(std::is_move_constructible<GgmlBackendGuard>::value);
		REQUIRE(std::is_move_assignable<GgmlBackendGuard>::value);
		REQUIRE_FALSE(std::is_copy_constructible<GgmlBackendGuard>::value);
		REQUIRE_FALSE(std::is_copy_assignable<GgmlBackendGuard>::value);

		REQUIRE(std::is_move_constructible<GgmlBackendBufferGuard>::value);
		REQUIRE(std::is_move_assignable<GgmlBackendBufferGuard>::value);
		REQUIRE_FALSE(std::is_copy_constructible<GgmlBackendBufferGuard>::value);
		REQUIRE_FALSE(std::is_copy_assignable<GgmlBackendBufferGuard>::value);

		REQUIRE(std::is_move_constructible<GgmlBackendSchedGuard>::value);
		REQUIRE(std::is_move_assignable<GgmlBackendSchedGuard>::value);
		REQUIRE_FALSE(std::is_copy_constructible<GgmlBackendSchedGuard>::value);
		REQUIRE_FALSE(std::is_copy_assignable<GgmlBackendSchedGuard>::value);
	}
}

TEST_CASE("Guard noexcept specifications", "[resource_guards]") {
	SECTION("Move operations are noexcept") {
		REQUIRE(std::is_nothrow_move_constructible<GgmlBackendGuard>::value);
		REQUIRE(std::is_nothrow_move_assignable<GgmlBackendGuard>::value);

		REQUIRE(std::is_nothrow_move_constructible<GgmlBackendBufferGuard>::value);
		REQUIRE(std::is_nothrow_move_assignable<GgmlBackendBufferGuard>::value);

		REQUIRE(std::is_nothrow_move_constructible<GgmlBackendSchedGuard>::value);
		REQUIRE(std::is_nothrow_move_assignable<GgmlBackendSchedGuard>::value);
	}
}
