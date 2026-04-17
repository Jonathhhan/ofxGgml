#include "catch2.hpp"
#include "../src/core/ofxGgmlHelpers.h"

TEST_CASE("Type name conversions", "[helpers]") {
	SECTION("F32 type") {
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::F32) == "f32");
	}

	SECTION("F16 type") {
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::F16) == "f16");
	}

	SECTION("Quantized types") {
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::Q4_0) == "q4_0");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::Q4_1) == "q4_1");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::Q5_0) == "q5_0");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::Q5_1) == "q5_1");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::Q8_0) == "q8_0");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::Q8_1) == "q8_1");
	}

	SECTION("Integer types") {
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::I8) == "i8");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::I16) == "i16");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::I32) == "i32");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::I64) == "i64");
	}

	SECTION("Other float types") {
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::F64) == "f64");
		REQUIRE(ofxGgmlHelpers::typeName(ofxGgmlType::BF16) == "bf16");
	}
}

TEST_CASE("Backend type name conversions", "[helpers]") {
	SECTION("CPU backend") {
		REQUIRE(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Cpu) == "CPU");
	}

	SECTION("GPU backend") {
		REQUIRE(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Gpu) == "GPU");
	}

	SECTION("Integrated GPU backend") {
		REQUIRE(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::IntegratedGpu) == "Integrated GPU");
	}

	SECTION("Accelerator backend") {
		REQUIRE(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Accelerator) == "Accelerator");
	}
}

TEST_CASE("Element size calculations", "[helpers]") {
	SECTION("Float types") {
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::F32) == 4);
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::F16) == 2);
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::BF16) == 2);
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::F64) == 8);
	}

	SECTION("Integer types") {
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::I8) == 1);
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::I16) == 2);
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::I32) == 4);
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::I64) == 8);
	}

	SECTION("Quantized types return 0") {
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::Q4_0) == 0);
		REQUIRE(ofxGgmlHelpers::elementSize(ofxGgmlType::Q8_0) == 0);
	}
}

TEST_CASE("Pool type name conversions", "[helpers]") {
	SECTION("Average pooling") {
		REQUIRE(ofxGgmlHelpers::poolTypeName(ofxGgmlPoolType::Avg) == "AVG");
	}

	SECTION("Max pooling") {
		REQUIRE(ofxGgmlHelpers::poolTypeName(ofxGgmlPoolType::Max) == "MAX");
	}
}

TEST_CASE("State name conversions", "[helpers]") {
	SECTION("Uninitialized state") {
		REQUIRE(ofxGgmlHelpers::stateName(ofxGgmlState::Uninitialized) == "Uninitialized");
	}

	SECTION("Ready state") {
		REQUIRE(ofxGgmlHelpers::stateName(ofxGgmlState::Ready) == "Ready");
	}

	SECTION("Computing state") {
		REQUIRE(ofxGgmlHelpers::stateName(ofxGgmlState::Computing) == "Computing");
	}

	SECTION("Error state") {
		REQUIRE(ofxGgmlHelpers::stateName(ofxGgmlState::Error) == "Error");
	}
}

TEST_CASE("Format bytes", "[helpers]") {
	SECTION("Bytes") {
		std::string result = ofxGgmlHelpers::formatBytes(512);
		REQUIRE(result.find("512") != std::string::npos);
		REQUIRE(result.find("B") != std::string::npos);
	}

	SECTION("Kilobytes") {
		std::string result = ofxGgmlHelpers::formatBytes(2048);
		REQUIRE(result.find("2.00") != std::string::npos);
		REQUIRE(result.find("KB") != std::string::npos);
	}

	SECTION("Megabytes") {
		std::string result = ofxGgmlHelpers::formatBytes(5 * 1024 * 1024);
		REQUIRE(result.find("5.00") != std::string::npos);
		REQUIRE(result.find("MB") != std::string::npos);
	}

	SECTION("Gigabytes") {
		std::string result = ofxGgmlHelpers::formatBytes(3ULL * 1024 * 1024 * 1024);
		REQUIRE(result.find("3.00") != std::string::npos);
		REQUIRE(result.find("GB") != std::string::npos);
	}

	SECTION("Zero bytes") {
		std::string result = ofxGgmlHelpers::formatBytes(0);
		REQUIRE(result.find("0.00") != std::string::npos);
		REQUIRE(result.find("B") != std::string::npos);
	}
}

TEST_CASE("Format duration in milliseconds", "[helpers]") {
	SECTION("Milliseconds") {
		std::string result = ofxGgmlHelpers::formatDurationMs(5.25);
		REQUIRE(result.find("5.25") != std::string::npos);
		REQUIRE(result.find("ms") != std::string::npos);
	}

	SECTION("Microseconds for sub-millisecond") {
		std::string result = ofxGgmlHelpers::formatDurationMs(0.5);
		REQUIRE(result.find("500") != std::string::npos);
		REQUIRE(result.find("us") != std::string::npos);
	}

	SECTION("Zero duration") {
		std::string result = ofxGgmlHelpers::formatDurationMs(0.0);
		REQUIRE(result.find("0.00") != std::string::npos);
		REQUIRE(result.find("ms") != std::string::npos);
	}

	SECTION("Large milliseconds") {
		std::string result = ofxGgmlHelpers::formatDurationMs(1234.56);
		REQUIRE(result.find("1234.56") != std::string::npos);
		REQUIRE(result.find("ms") != std::string::npos);
	}

	SECTION("Custom decimal places") {
		std::string result = ofxGgmlHelpers::formatDurationMs(12.3456, 3);
		REQUIRE(result.find("12.346") != std::string::npos);
		REQUIRE(result.find("ms") != std::string::npos);
	}
}

TEST_CASE("Format rate with SI prefixes", "[helpers]") {
	SECTION("Base units") {
		std::string result = ofxGgmlHelpers::formatRate(500.0, "Hz");
		REQUIRE(result.find("500") != std::string::npos);
		REQUIRE(result.find("Hz") != std::string::npos);
	}

	SECTION("Kilo prefix") {
		std::string result = ofxGgmlHelpers::formatRate(1500.0, "Hz");
		REQUIRE(result.find("1.50") != std::string::npos);
		REQUIRE(result.find("KHz") != std::string::npos);
	}

	SECTION("Mega prefix") {
		std::string result = ofxGgmlHelpers::formatRate(2500000.0, "Hz");
		REQUIRE(result.find("2.50") != std::string::npos);
		REQUIRE(result.find("MHz") != std::string::npos);
	}

	SECTION("Giga prefix") {
		std::string result = ofxGgmlHelpers::formatRate(3.5e9, "Hz");
		REQUIRE(result.find("3.50") != std::string::npos);
		REQUIRE(result.find("GHz") != std::string::npos);
	}

	SECTION("Zero rate") {
		std::string result = ofxGgmlHelpers::formatRate(0.0, "ops");
		REQUIRE(result.find("0.00") != std::string::npos);
		REQUIRE(result.find("ops") != std::string::npos);
	}

	SECTION("Null unit") {
		std::string result = ofxGgmlHelpers::formatRate(1000.0, nullptr);
		REQUIRE(result.find("1.00") != std::string::npos);
		REQUIRE(result.find("K") != std::string::npos);
	}
}

TEST_CASE("Format FLOPS", "[helpers]") {
	SECTION("GFLOPS") {
		std::string result = ofxGgmlHelpers::formatFlops(2.5e9);
		REQUIRE(result.find("2.50") != std::string::npos);
		REQUIRE(result.find("GFLOP/s") != std::string::npos);
	}

	SECTION("MFLOPS") {
		std::string result = ofxGgmlHelpers::formatFlops(500e6);
		REQUIRE(result.find("500") != std::string::npos);
		REQUIRE(result.find("MFLOP/s") != std::string::npos);
	}

	SECTION("TFLOPS") {
		std::string result = ofxGgmlHelpers::formatFlops(1.2e12);
		REQUIRE(result.find("1.20") != std::string::npos);
		REQUIRE(result.find("TFLOP/s") != std::string::npos);
	}
}

TEST_CASE("Format operations per second", "[helpers]") {
	SECTION("Regular ops/s") {
		std::string result = ofxGgmlHelpers::formatOpsPerSecond(800.0);
		REQUIRE(result.find("800") != std::string::npos);
		REQUIRE(result.find("ops/s") != std::string::npos);
	}

	SECTION("Kops/s") {
		std::string result = ofxGgmlHelpers::formatOpsPerSecond(45000.0);
		REQUIRE(result.find("45.00") != std::string::npos);
		REQUIRE(result.find("Kops/s") != std::string::npos);
	}

	SECTION("Mops/s") {
		std::string result = ofxGgmlHelpers::formatOpsPerSecond(3.2e6);
		REQUIRE(result.find("3.20") != std::string::npos);
		REQUIRE(result.find("Mops/s") != std::string::npos);
	}
}
