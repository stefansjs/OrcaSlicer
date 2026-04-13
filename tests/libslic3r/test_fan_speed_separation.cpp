#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ExtrusionEntity.hpp"

using namespace Slic3r;

SCENARIO("Fan speed separation: Config validation", "[Config][FanSpeed]") {
    GIVEN("A default print config") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        WHEN("Checking for overhang_fan_speed setting") {
            THEN("The setting exists and has default value 100") {
                REQUIRE(config.has("overhang_fan_speed"));
                REQUIRE(config.opt<ConfigOptionInts>("overhang_fan_speed")->get_at(0) == 100);
            }
        }
        
        WHEN("Checking for external_bridge_fan_speed setting") {
            THEN("The setting exists and has default value 100") {
                REQUIRE(config.has("external_bridge_fan_speed"));
                REQUIRE(config.opt<ConfigOptionInts>("external_bridge_fan_speed")->get_at(0) == 100);
            }
        }
        
        WHEN("Setting overhang_fan_speed to 75") {
            config.set_key_value("overhang_fan_speed", new ConfigOptionInts{75});
            THEN("The value is set correctly") {
                REQUIRE(config.opt<ConfigOptionInts>("overhang_fan_speed")->get_at(0) == 75);
            }
        }
        
        WHEN("Setting external_bridge_fan_speed to 80") {
            config.set_key_value("external_bridge_fan_speed", new ConfigOptionInts{80});
            THEN("The value is set correctly") {
                REQUIRE(config.opt<ConfigOptionInts>("external_bridge_fan_speed")->get_at(0) == 80);
            }
        }
        
        WHEN("Setting both fan speeds independently") {
            config.set_key_value("overhang_fan_speed", new ConfigOptionInts{60});
            config.set_key_value("external_bridge_fan_speed", new ConfigOptionInts{90});
            THEN("Both values are set independently") {
                REQUIRE(config.opt<ConfigOptionInts>("overhang_fan_speed")->get_at(0) == 60);
                REQUIRE(config.opt<ConfigOptionInts>("external_bridge_fan_speed")->get_at(0) == 90);
            }
        }
        
        WHEN("Validating config with valid fan speeds") {
            config.set_key_value("overhang_fan_speed", new ConfigOptionInts{50});
            config.set_key_value("external_bridge_fan_speed", new ConfigOptionInts{75});
            THEN("Config validation passes") {
                REQUIRE(config.validate().empty());
            }
        }
    }
}

SCENARIO("Fan speed separation: PrintConfig access", "[PrintConfig][FanSpeed]") {
    GIVEN("A PrintConfig with fan speed settings") {
        PrintConfig print_config;
        print_config.overhang_fan_speed.values = {100};
        print_config.external_bridge_fan_speed.values = {100};
        
        WHEN("Accessing overhang_fan_speed") {
            THEN("The value is accessible") {
                REQUIRE(print_config.overhang_fan_speed.get_at(0) == 100);
            }
        }
        
        WHEN("Accessing external_bridge_fan_speed") {
            THEN("The value is accessible") {
                REQUIRE(print_config.external_bridge_fan_speed.get_at(0) == 100);
            }
        }
        
        WHEN("Setting different values") {
            print_config.overhang_fan_speed = ConfigOptionInts{60};
            print_config.external_bridge_fan_speed = ConfigOptionInts{90};
            THEN("Both values are set independently") {
                REQUIRE(print_config.overhang_fan_speed.get_at(0) == 60);
                REQUIRE(print_config.external_bridge_fan_speed.get_at(0) == 90);
            }
        }
    }
}

SCENARIO("Fan speed separation: ExtrusionRole distinction", "[ExtrusionRole][FanSpeed]") {
    GIVEN("Different ExtrusionRole types") {
        WHEN("Checking erOverhangPerimeter") {
            THEN("It should be identified as an overhang") {
                REQUIRE(is_perimeter(erOverhangPerimeter));
                REQUIRE(!is_infill(erOverhangPerimeter));
            }
        }
        
        WHEN("Checking erBridgeInfill") {
            THEN("It should be identified as an external bridge") {
                REQUIRE(is_infill(erBridgeInfill));
                REQUIRE(is_bridge(erBridgeInfill));
                REQUIRE(!is_perimeter(erBridgeInfill));
            }
        }
        
        WHEN("Checking erInternalBridgeInfill") {
            THEN("It should be identified as an internal bridge") {
                REQUIRE(is_infill(erInternalBridgeInfill));
                REQUIRE(is_bridge(erInternalBridgeInfill));
                REQUIRE(!is_perimeter(erInternalBridgeInfill));
            }
        }
        
        WHEN("Comparing roles") {
            THEN("erOverhangPerimeter and erBridgeInfill are distinct") {
                REQUIRE(erOverhangPerimeter != erBridgeInfill);
                REQUIRE(erBridgeInfill != erInternalBridgeInfill);
                REQUIRE(erOverhangPerimeter != erInternalBridgeInfill);
            }
            THEN("erBridgeInfill is recognized as a bridge") {
                REQUIRE(is_bridge(erBridgeInfill));
            }
            THEN("erOverhangPerimeter is recognized as a bridge") {
                REQUIRE(is_bridge(erOverhangPerimeter));
            }
        }
    }
}

SCENARIO("Fan speed separation: Bridge perimeter role logic", "[ExtrusionRole][FanSpeed][Bridge]") {
    GIVEN("Perimeter role assignment logic") {
        WHEN("A perimeter is over air (bridge)") {
            THEN("It should be assigned erBridgeInfill to trigger bridge fan speed") {
                // This logic is implemented in PerimeterGenerator.cpp
                // We verify the constants here to ensure they remain consistent with GCode.cpp
                REQUIRE(is_bridge(erBridgeInfill));
                REQUIRE(is_perimeter(erBridgeInfill) == false); // erBridgeInfill is technically infill but used for bridge perimeters in Orca
            }
        }
        WHEN("A perimeter is an overhang but not a full bridge") {
            THEN("It should be assigned erOverhangPerimeter to trigger overhang fan speed") {
                REQUIRE(is_bridge(erOverhangPerimeter));
                REQUIRE(is_perimeter(erOverhangPerimeter));
            }
        }
        WHEN("Checking role properties for fan speed selection") {
            THEN("erBridgeInfill is a bridge but not a perimeter") {
                // In OrcaSlicer, bridge perimeters are promoted to erBridgeInfill
                // so they get bridge-specific flow and fan settings.
                REQUIRE(is_bridge(erBridgeInfill));
                REQUIRE(!is_perimeter(erBridgeInfill));
            }
            THEN("erOverhangPerimeter is both a bridge and a perimeter") {
                // Non-bridge overhangs use this role.
                REQUIRE(is_bridge(erOverhangPerimeter));
                REQUIRE(is_perimeter(erOverhangPerimeter));
            }
        }
    }
}

SCENARIO("Fan speed separation: Profile compatibility", "[Preset][FanSpeed]") {
    GIVEN("A config with old overhang_fan_speed setting") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_key_value("overhang_fan_speed", new ConfigOptionInts{100});
        
        WHEN("Loading the config") {
            THEN("external_bridge_fan_speed defaults to 100") {
                REQUIRE(config.opt<ConfigOptionInts>("external_bridge_fan_speed")->get_at(0) == 100);
            }
            THEN("overhang_fan_speed retains its value") {
                REQUIRE(config.opt<ConfigOptionInts>("overhang_fan_speed")->get_at(0) == 100);
            }
        }
    }
    
    GIVEN("A config with both settings") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_key_value("overhang_fan_speed", new ConfigOptionInts{60});
        config.set_key_value("external_bridge_fan_speed", new ConfigOptionInts{90});
        
        WHEN("Serializing and deserializing") {
            std::string serialized;
            serialized += "overhang_fan_speed = " + config.opt_serialize("overhang_fan_speed") + "\n";
            serialized += "external_bridge_fan_speed = " + config.opt_serialize("external_bridge_fan_speed") + "\n";
            
            DynamicPrintConfig new_config = DynamicPrintConfig::full_print_config();
            new_config.load_from_ini_string(serialized, ForwardCompatibilitySubstitutionRule::Disable);
            
            THEN("Both settings are set independently") {
                REQUIRE(new_config.opt<ConfigOptionInts>("overhang_fan_speed")->get_at(0) == 60);
                REQUIRE(new_config.opt<ConfigOptionInts>("external_bridge_fan_speed")->get_at(0) == 90);
            }
        }
    }
}

SCENARIO("Fan speed separation: Fan speed control configuration", "[PrintConfig][FanSpeed]") {
    GIVEN("A PrintConfig with different fan speeds configured") {
        PrintConfig print_config;
        print_config.enable_overhang_bridge_fan = ConfigOptionBools{true};
        print_config.overhang_fan_speed = ConfigOptionInts{70};
        print_config.external_bridge_fan_speed = ConfigOptionInts{90};
        print_config.fan_min_speed = ConfigOptionFloats{0.0f};
        print_config.fan_max_speed = ConfigOptionFloats{100.0f};
        print_config.close_fan_the_first_x_layers = ConfigOptionInts{0};
        
        WHEN("Checking overhang fan speed") {
            THEN("The value is set correctly") {
                REQUIRE(print_config.overhang_fan_speed.get_at(0) == 70);
            }
        }
        
        WHEN("Checking external bridge fan speed") {
            THEN("The value is set correctly") {
                REQUIRE(print_config.external_bridge_fan_speed.get_at(0) == 90);
            }
        }
        
        WHEN("enable_overhang_bridge_fan is disabled") {
            // Don't assign a raw bool here: 'false' is a null pointer constant and could bind to
            // ConfigOptionBools::operator=(const ConfigOption*), causing a crash.
            print_config.enable_overhang_bridge_fan = ConfigOptionBools{false};
            THEN("The setting is disabled") {
                REQUIRE(print_config.enable_overhang_bridge_fan.get_at(0) == false);
            }
        }
    }
}

SCENARIO("Fan speed separation: Boundary conditions", "[Config][FanSpeed]") {
    GIVEN("A config with boundary values") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        WHEN("Setting overhang_fan_speed to minimum (0)") {
            config.set_key_value("overhang_fan_speed", new ConfigOptionInts{0});
            THEN("The value is accepted") {
                REQUIRE(config.opt<ConfigOptionInts>("overhang_fan_speed")->get_at(0) == 0);
                REQUIRE(config.validate().empty());
            }
        }
        
        WHEN("Setting overhang_fan_speed to maximum (100)") {
            config.set_key_value("overhang_fan_speed", new ConfigOptionInts{100});
            THEN("The value is accepted") {
                REQUIRE(config.opt<ConfigOptionInts>("overhang_fan_speed")->get_at(0) == 100);
                REQUIRE(config.validate().empty());
            }
        }
        
        WHEN("Setting external_bridge_fan_speed to minimum (0)") {
            config.set_key_value("external_bridge_fan_speed", new ConfigOptionInts{0});
            THEN("The value is accepted") {
                REQUIRE(config.opt<ConfigOptionInts>("external_bridge_fan_speed")->get_at(0) == 0);
                REQUIRE(config.validate().empty());
            }
        }
        
        WHEN("Setting external_bridge_fan_speed to maximum (100)") {
            config.set_key_value("external_bridge_fan_speed", new ConfigOptionInts{100});
            THEN("The value is accepted") {
                REQUIRE(config.opt<ConfigOptionInts>("external_bridge_fan_speed")->get_at(0) == 100);
                REQUIRE(config.validate().empty());
            }
        }
    }
}
