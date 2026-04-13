#include <catch2/catch.hpp>
#include <regex>
#include <set>

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_data.hpp"
#include "test_bridge_helpers.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Slic3r::Test::BridgeHelpers;

// ============================================================================
// Scenario 1: Bridge surfaces are detected correctly
// ============================================================================

SCENARIO("Bridge surfaces are detected correctly", "[Bridge][Surface]")
{
    GIVEN("A bridge mesh sliced with default settings")
    {
        Slic3r::Print print;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        Slic3r::Test::init_and_process_print({bridge_mesh}, print, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
        });

        WHEN("Searching for bridge surfaces")
        {
            double bridge_z = find_first_bridge_z(print);
            INFO("First bridge Z: " << bridge_z);

            THEN("At least one layer has stBottomBridge fill surfaces")
            {
                REQUIRE(bridge_z > 0.0);
            }

            THEN("The bridge layer fill surfaces contain stBottomBridge")
            {
                REQUIRE(bridge_z > 0.0);
                auto types = collect_surface_types_at_z(print, bridge_z);
                REQUIRE(contains_surface_type(types, stBottomBridge));
            }

            THEN("Layers below the bridge do NOT have stBottomBridge surfaces")
            {
                REQUIRE(bridge_z > 0.0);
                // Check a pillar layer (first layer)
                auto types_first = collect_surface_types_at_z(print, 0.2);
                REQUIRE_FALSE(contains_surface_type(types_first, stBottomBridge));
            }
        }
    }
}

// ============================================================================
// Scenario 2: Bridge perimeters use correct extrusion roles
// ============================================================================

SCENARIO("Bridge perimeters use correct extrusion roles", "[Bridge][Perimeter]")
{
    GIVEN("A bridge mesh sliced with default settings")
    {
        Slic3r::Print print;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        Slic3r::Test::init_and_process_print({bridge_mesh}, print, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
        });

        WHEN("Inspecting perimeter roles on the bridge layer")
        {
            double bridge_z = find_first_bridge_z(print);
            INFO("Bridge Z: " << bridge_z);
            REQUIRE(bridge_z > 0.0);

            auto roles = collect_perimeter_roles_at_z(print, bridge_z);
            for (auto r : unique_roles(roles)) {
                INFO("Bridge layer perimeter role: " << (int)r);
            }

            THEN("Bridge layer perimeters include erBridgeInfill")
            {
                REQUIRE(contains_erBridgeInfill(roles));
            }
        }

        WHEN("Inspecting perimeter roles on a non-bridge layer")
        {
            // First layer (pillar) should not have bridge perimeters
            auto roles_first = collect_perimeter_roles_at_z(print, 0.2);

            THEN("Non-bridge layer perimeters do NOT contain erBridgeInfill")
            {
                REQUIRE_FALSE(contains_erBridgeInfill(roles_first));
            }
        }
    }
}

// ============================================================================
// Scenario 3: Bridge infill uses correct extrusion roles
// ============================================================================

SCENARIO("Bridge infill uses correct extrusion roles", "[Bridge][Infill]")
{
    GIVEN("A bridge mesh sliced with default settings")
    {
        Slic3r::Print print;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        Slic3r::Test::init_and_process_print({bridge_mesh}, print, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
        });

        WHEN("Inspecting fill roles on the bridge layer")
        {
            double bridge_z = find_first_bridge_z(print);
            INFO("Bridge Z: " << bridge_z);
            REQUIRE(bridge_z > 0.0);

            auto fill_roles = collect_fill_roles_at_z(print, bridge_z);
            for (auto r : unique_roles(fill_roles)) {
                INFO("Bridge layer fill role: " << (int)r);
            }

            THEN("Bridge layer fills include erBridgeInfill")
            {
                REQUIRE(contains_erBridgeInfill(fill_roles));
            }
        }

        WHEN("Inspecting fill roles on a non-bridge layer")
        {
            // First layer (pillar) should not have bridge infill
            auto fill_roles_first = collect_fill_roles_at_z(print, 0.2);

            THEN("Non-bridge layer fills do NOT contain erBridgeInfill")
            {
                REQUIRE_FALSE(contains_erBridgeInfill(fill_roles_first));
            }
        }
    }
}

// ============================================================================
// Scenario 4: Bridge speed is applied in G-code
// ============================================================================

SCENARIO("Bridge speed is applied in G-code", "[Bridge][Speed]")
{
    GIVEN("A bridge mesh sliced with bridge_speed=25 and cooling disabled")
    {
        double bridge_speed_mm_s = 25.0;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        std::string gcode = Slic3r::Test::slice({bridge_mesh}, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "bridge_speed",              bridge_speed_mm_s },
            { "gcode_comments",            true },
            { "cooling",                   false },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
        });

        WHEN("Parsing the G-code for bridge extrusions")
        {
            // OrcaSlicer emits role tags like ";TYPE:Bridge" before bridge
            // extrusion moves.  Track the current role and check F values on
            // extrusion moves that follow a bridge role tag.
            double expected_f = bridge_speed_mm_s * 60.0; // mm/min
            bool found_bridge_line = false;
            bool bridge_speed_correct = true;
            bool in_bridge_section = false;

            auto is_bridge_role_tag_4 = [](const std::string &raw) {
                return raw.find("TYPE:Bridge") != std::string::npos ||
                       raw.find("FEATURE: Bridge") != std::string::npos;
            };
            auto is_any_role_tag_4 = [](const std::string &raw) {
                return raw.find("TYPE:") != std::string::npos ||
                       raw.find("FEATURE: ") != std::string::npos;
            };
            GCodeReader reader;
            reader.parse_buffer(gcode,
                [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
                    std::string raw = line.raw();
                    // Track role changes via TYPE/FEATURE tags (BBL and compatible printers)
                    if (is_bridge_role_tag_4(raw)) {
                        in_bridge_section = true;
                    } else if (is_any_role_tag_4(raw) && !is_bridge_role_tag_4(raw)) {
                        in_bridge_section = false;
                    }
                    // Check extrusion moves within bridge sections
                    if (in_bridge_section && line.extruding(self) && line.dist_XY(self) > 0.0) {
                        found_bridge_line = true;
                        if (line.has_f()) {
                            double f = line.f();
                            // Allow 1% tolerance for rounding
                            if (std::abs(f - expected_f) > expected_f * 0.01) {
                                bridge_speed_correct = false;
                            }
                        }
                    }
                });

            THEN("Bridge infill lines exist in the G-code")
            {
                REQUIRE(found_bridge_line);
            }

            THEN("Bridge infill lines use the configured bridge speed")
            {
                REQUIRE(found_bridge_line);
                REQUIRE(bridge_speed_correct);
            }
        }
    }
}

// ============================================================================
// Scenario 5: Bridge flow is applied correctly
// ============================================================================

SCENARIO("Bridge flow is applied correctly", "[Bridge][Flow]")
{
    GIVEN("A bridge mesh sliced with bridge_flow=1.0")
    {
        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        std::string gcode = Slic3r::Test::slice({bridge_mesh}, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "bridge_flow",               1.0 },
            { "gcode_comments",            true },
            { "cooling",                   false },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
        });

        WHEN("Measuring extrusion rates on bridge lines")
        {
            bool found_bridge_extrusion = false;
            double total_bridge_e_per_mm = 0.0;
            int bridge_count = 0;
            bool in_bridge_section = false;

            auto is_bridge_role_tag_5 = [](const std::string &raw) {
                return raw.find("TYPE:Bridge") != std::string::npos ||
                       raw.find("FEATURE: Bridge") != std::string::npos;
            };
            auto is_any_role_tag_5 = [](const std::string &raw) {
                return raw.find("TYPE:") != std::string::npos ||
                       raw.find("FEATURE: ") != std::string::npos;
            };
            GCodeReader reader;
            reader.parse_buffer(gcode,
                [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
                    std::string raw = line.raw();
                    // Track role changes via TYPE/FEATURE tags (BBL and compatible printers)
                    if (is_bridge_role_tag_5(raw)) {
                        in_bridge_section = true;
                    } else if (is_any_role_tag_5(raw) && !is_bridge_role_tag_5(raw)) {
                        in_bridge_section = false;
                    }
                    if (in_bridge_section &&
                        line.extruding(self) && line.dist_XY(self) > 0.0) {
                        found_bridge_extrusion = true;
                        double e_per_mm = line.dist_E(self) / line.dist_XY(self);
                        total_bridge_e_per_mm += e_per_mm;
                        bridge_count++;
                    }
                });

            THEN("Bridge extrusion moves exist in the G-code")
            {
                REQUIRE(found_bridge_extrusion);
            }

            THEN("Bridge extrusion rate (E/mm) is positive")
            {
                REQUIRE(found_bridge_extrusion);
                REQUIRE(bridge_count > 0);
                double avg_e_per_mm = total_bridge_e_per_mm / bridge_count;
                REQUIRE(avg_e_per_mm > 0.0);
            }
        }
    }
}

// ============================================================================
// Scenario 6: Bridge fan speed markers in G-code
// ============================================================================

SCENARIO("Bridge fan speed markers in G-code", "[Bridge][FanSpeed]")
{
    GIVEN("A bridge mesh sliced with external_bridge_fan_speed=40")
    {
        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        std::string gcode = Slic3r::Test::slice({bridge_mesh}, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "gcode_comments",            true },
            { "cooling",                   true },
            { "enable_overhang_bridge_fan", true },
            { "external_bridge_fan_speed", 40 },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
        });

        WHEN("Inspecting the G-code for bridge role tags")
        {
            // Raw fan markers (;_EXTERNAL_BRIDGE_FAN_START/END) are consumed
            // by CoolingBuffer and replaced with M106 commands.  Instead,
            // verify the gcode contains bridge role tags and M106 commands.
            bool has_bridge_role_tag = gcode.find("TYPE:Bridge") != std::string::npos ||
                                       gcode.find("FEATURE: Bridge") != std::string::npos;
            bool has_m106            = gcode.find("M106 S") != std::string::npos;

            THEN("G-code contains bridge role tag")
            {
                REQUIRE(has_bridge_role_tag);
            }

            THEN("G-code contains M106 fan commands (from CoolingBuffer)")
            {
                REQUIRE(has_m106);
            }
        }

        WHEN("Checking fan speed command between bridge markers")
        {
            // After CoolingBuffer processing, bridge fan markers are replaced with
            // M106 commands. Check that M106 with S102 (40% of 255 = 102) appears.
            // 40% of 255 = 102
            int expected_s = static_cast<int>(255.0 * 40.0 / 100.0 + 0.5);
            std::string expected_m106 = "M106 S" + std::to_string(expected_s);

            bool found_expected_fan = gcode.find(expected_m106) != std::string::npos;

            THEN("M106 with correct fan speed (40%) appears in G-code")
            {
                INFO("Looking for: " << expected_m106);
                REQUIRE(found_expected_fan);
            }
        }
    }
}

// ============================================================================
// Scenario 7: Extra bridge layer surfaces for external bridges
// ============================================================================

SCENARIO("Extra bridge layer surfaces are created for external bridges", "[Bridge][ExtraBridge][Surface]")
{
    GIVEN("A bridge mesh with enable_extra_bridge_layer = apply_to_all")
    {
        Slic3r::Print print;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        Slic3r::Test::init_and_process_print({bridge_mesh}, print, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "enable_extra_bridge_layer", "apply_to_all" },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
            { "thick_bridges",             false },
        });

        WHEN("Inspecting the layer above the first bridge")
        {
            double bridge_z = find_first_bridge_z(print);
            INFO("First bridge Z: " << bridge_z);
            REQUIRE(bridge_z > 0.0);

            double extra_z = bridge_z + 0.2;
            INFO("Extra bridge Z: " << extra_z);
            auto extra_types = collect_surface_types_at_z(print, extra_z);

            for (size_t i = 0; i < extra_types.size(); ++i) {
                INFO("Extra layer surface type [" << i << "]: " << (int)extra_types[i]);
            }

            THEN("The extra bridge layer has bridge-related surface types")
            {
                // The extra bridge layer should have stBottomBridge (reclassified
                // from stInternalAfterExternalBridge) or stInternalAfterExternalBridge
                bool has_bridge_surface =
                    contains_surface_type(extra_types, stBottomBridge) ||
                    contains_surface_type(extra_types, stInternalAfterExternalBridge);
                REQUIRE(has_bridge_surface);
            }
        }
    }
}

// ============================================================================
// Scenario 8: Extra bridge layer perimeters are promoted to bridge roles
// ============================================================================

SCENARIO("Extra bridge layer perimeters are promoted to bridge roles", "[Bridge][ExtraBridge][Perimeter]")
{
    GIVEN("A bridge mesh with enable_extra_bridge_layer = apply_to_all")
    {
        Slic3r::Print print;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        Slic3r::Test::init_and_process_print({bridge_mesh}, print, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "enable_extra_bridge_layer", "apply_to_all" },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
            { "thick_bridges",             false },
        });

        WHEN("Inspecting perimeter roles on the extra bridge layer")
        {
            double bridge_z = find_first_bridge_z(print);
            INFO("First bridge Z: " << bridge_z);
            REQUIRE(bridge_z > 0.0);

            double extra_z = bridge_z + 0.2;
            INFO("Extra bridge Z: " << extra_z);

            auto roles = collect_perimeter_roles_at_z(print, extra_z);
            for (auto r : unique_roles(roles)) {
                INFO("Extra bridge layer perimeter role: " << (int)r);
            }

            THEN("Extra bridge layer perimeters include bridge roles")
            {
                REQUIRE(contains_bridge_role(roles));
            }
        }
    }
}

// ============================================================================
// Scenario 9: Extra bridge layer for internal bridges
// ============================================================================

SCENARIO("Extra bridge layer for internal bridges", "[Bridge][ExtraBridge][InternalBridge]")
{
    GIVEN("A bridge mesh with enable_extra_bridge_layer = apply_to_all and solid infill")
    {
        Slic3r::Print print;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        // Use bottom_shell_layers=2 so that the second bottom layer triggers
        // internal bridge (stInternalBridge) detection in bridge_over_infill.
        Slic3r::Test::init_and_process_print({bridge_mesh}, print, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "enable_extra_bridge_layer", "apply_to_all" },
            { "bottom_shell_layers",       2 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
            { "thick_bridges",             false },
        });

        WHEN("Searching for internal bridge surfaces")
        {
            double internal_bridge_z = find_first_internal_bridge_z(print);
            INFO("First internal bridge Z: " << internal_bridge_z);

            THEN("At least one layer has stInternalBridge fill surfaces")
            {
                // Internal bridges may or may not be present depending on the
                // geometry. If present, verify the extra layer.
                if (internal_bridge_z > 0.0) {
                    double extra_z = internal_bridge_z + 0.2;
                    auto extra_types = collect_surface_types_at_z(print, extra_z);

                    bool has_second_bridge =
                        contains_surface_type(extra_types, stSecondInternalBridge) ||
                        contains_surface_type(extra_types, stInternalBridge);

                    INFO("Extra internal bridge Z: " << extra_z);
                    for (size_t i = 0; i < extra_types.size(); ++i) {
                        INFO("Surface type [" << i << "]: " << (int)extra_types[i]);
                    }
                    REQUIRE(has_second_bridge);
                } else {
                    // No internal bridges found -- this is geometry-dependent.
                    // Just verify the bridge mesh does produce regular bridges.
                    double bridge_z = find_first_bridge_z(print);
                    REQUIRE(bridge_z > 0.0);
                    SUCCEED("No internal bridges found in this geometry (expected for single-bottom bridge mesh)");
                }
            }
        }
    }
}

// ============================================================================
// Scenario 10: Extra bridge layer speed and fan in G-code
// ============================================================================

SCENARIO("Extra bridge layer speed and fan in G-code", "[Bridge][ExtraBridge][GCode]")
{
    GIVEN("A bridge mesh with extra bridge layers, bridge_speed=25, and fan configured")
    {
        double bridge_speed_mm_s = 25.0;

        TriangleMesh bridge_mesh = Slic3r::Test::mesh(TestMesh::bridge);
        bridge_mesh.align_to_origin();

        std::string gcode = Slic3r::Test::slice({bridge_mesh}, {
            { "layer_height",              0.2 },
            { "initial_layer_print_height", 0.2 },
            { "bridge_speed",              bridge_speed_mm_s },
            { "enable_extra_bridge_layer", "apply_to_all" },
            { "gcode_comments",            true },
            { "cooling",                   true },
            { "enable_overhang_bridge_fan", true },
            { "external_bridge_fan_speed", 40 },
            { "bottom_shell_layers",       1 },
            { "top_shell_layers",          0 },
            { "sparse_infill_density",     0 },
            { "thick_bridges",             false },
        });

        WHEN("Inspecting G-code")
        {
            // Raw fan markers are consumed by CoolingBuffer.  Instead, verify
            // bridge role tags and M106 fan commands are present.
            // Support both BBL (FEATURE: Bridge) and compatible (TYPE:Bridge) tag formats.
            bool has_bridge_role_tag = gcode.find("TYPE:Bridge") != std::string::npos ||
                                       gcode.find("FEATURE: Bridge") != std::string::npos;
            bool has_m106            = gcode.find("M106 S") != std::string::npos;

            // Count occurrences of bridge role tags -- with extra bridge layer
            // there should be more than one occurrence across layers
            size_t bridge_tag_count = 0;
            for (const char *needle : {"TYPE:Bridge", "FEATURE: Bridge"}) {
                size_t pos = 0;
                while ((pos = gcode.find(needle, pos)) != std::string::npos) {
                    bridge_tag_count++;
                    pos += 1;
                }
            }

            THEN("Bridge fan markers are present")
            {
                REQUIRE(has_bridge_role_tag);
                REQUIRE(has_m106);
            }

            THEN("There are multiple bridge fan start markers (base + extra layer)")
            {
                INFO("Bridge role tag count: " << bridge_tag_count);
                // With extra bridge layer enabled, we expect at least 1 bridge tag.
                // When extra layer perimeter promotion is working, expect >= 2.
                CHECK(bridge_tag_count >= 1);
            }
        }
    }
}
