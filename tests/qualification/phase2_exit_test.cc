// Qualification tests: the Phase 2 exit conditions (Revised Plan,
// Section 17), in their CI-runnable form.
//
// Phase 2 promised closure under ECO transactions:
//
//   "Deliverable: repair, sizing, buffering and local resynthesis
//    adapters under ECO transactions, all-scene acceptance.
//    Exit condition: repairs preserve function and improve
//    post-route metrics across all required scenes versus the
//    pinned upstream baseline, at equal or better runtime."
//
// The unit suites prove each mechanism in isolation. This suite
// proves the exit condition's CI-runnable form, composed the way
// a real session runs it: the L5 closure loop screening sizing,
// buffering and resynthesis candidates through the L4 ECO
// transaction protocol, judged on every required scene, with the
// ledger and the loop's report binding as the record. One test
// per clause:
//
//   - improvement across ALL required scenes, not just one
//     (SizingImprovesEveryRequiredSceneAndCloses)
//   - a regression candidate is refused by the all-scene rule,
//     leaving design and ledger exactly as if never attempted
//     (CrossSceneRegressionIsRefusedByTheAllSceneRule)
//   - the fixture set is broader than one design: a second,
//     high-fanout design closes by buffering
//     (BufferingImprovesTheHighFanoutFixture)
//   - repairs preserve function: sizing keeps every connection
//     (asserted in the first test), and a logic-changing repair
//     stands only on an equivalence proof of the full design
//     (ResynthesisStandsOnlyOnAnEquivalenceProof,
//      ResynthesisThroughTheLoopPreservesFunctionAndCloses)
//   - a deliberately corrupted logic change is proved
//     not-equivalent and rolled back completely
//     (CorruptedSpliceIsProvedNotEquivalentAndRolledBack)
//
// What this suite does NOT prove, stated plainly: the exit
// condition's comparison against the pinned upstream baseline at
// equal or better runtime. That comparison is the gcd A/B of
// unit 2.5, a local evidence run on the Phase 0 routed design
// (the pinned upstream binary cannot run in CI), and its measured
// result was that EOS did NOT meet the condition on gcd: neither
// tool closed the degraded design, upstream improved the metrics
// far more, and upstream was far faster. The numbers are in
// docs/phase2-exit-report.md, and the exit decision they bear on
// is the project owner's, not this suite's. These tests prove
// the properties the exit condition requires of the repairs
// themselves, on committed synthetic fixtures whose expected
// numbers are fixture construction values, not measurements of
// any real cell.
#include "eos/session/flow.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "eos/eco/timing_eco.h"
#include "eos/session/reports.h"

namespace eos {
namespace {

using foundation::ErrorCode;
using session::CloseTimingRequest;
using session::Project;
using session::ProofStatus;
using session::ReportBinding;
using session::ReportRegistry;
using session::RunTimingClosure;
using session::TimingGateMet;

const char kLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal1
MACRO DFF
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN D
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END D
  PIN CK
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END CK
  PIN Q
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Q
END DFF
MACRO BUF_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END BUF_X1
MACRO BUF_X2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END BUF_X2
MACRO BUF_X4
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END BUF_X4
MACRO WIDE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN B
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END B
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END WIDE
END LIBRARY
)LEF";

std::string ChainDef(const std::string& buffer_master) {
  return std::string(R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 3 ;
- r1 DFF + PLACED ( 1000 1000 ) N ;
- u1 )DEF") +
         buffer_master + R"DEF( + PLACED ( 2000 1000 ) N ;
- r2 DFF + PLACED ( 3000 1000 ) N ;
END COMPONENTS
PINS 3 ;
- clk + NET clk + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- d + NET d + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 6000 ) N ;
- q + NET q + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
END PINS
NETS 5 ;
- clk ( PIN clk ) ( r1 CK ) ( r2 CK ) ;
- d ( PIN d ) ( r1 D ) ;
- n1 ( r1 Q ) ( u1 A ) ;
- n2 ( u1 Y ) ( r2 D ) ;
- q ( r2 Q ) ( PIN q ) ;
END NETS
END DESIGN
)DEF";
}


const char kFanoutDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 9 ;
- r1 DFF + PLACED ( 1000 1000 ) N ;
- r2 DFF + PLACED ( 2000 1000 ) N ;
- r3 DFF + PLACED ( 3000 1000 ) N ;
- r4 DFF + PLACED ( 4000 1000 ) N ;
- r5 DFF + PLACED ( 5000 1000 ) N ;
- r6 DFF + PLACED ( 6000 1000 ) N ;
- r7 DFF + PLACED ( 7000 1000 ) N ;
- r8 DFF + PLACED ( 8000 1000 ) N ;
- r9 DFF + PLACED ( 9000 1000 ) N ;
END COMPONENTS
PINS 2 ;
- clk + NET clk + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- d + NET d + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 6000 ) N ;
END PINS
NETS 3 ;
- clk ( PIN clk ) ( r1 CK ) ( r2 CK ) ( r3 CK ) ( r4 CK ) ( r5 CK ) ( r6 CK ) ( r7 CK ) ( r8 CK ) ( r9 CK ) ;
- d ( PIN d ) ( r1 D ) ;
- wide ( r1 Q ) ( r2 D ) ( r3 D ) ( r4 D ) ( r5 D ) ( r6 D ) ( r7 D ) ( r8 D ) ( r9 D ) ;
END NETS
END DESIGN
)DEF";

std::string ChainLiberty(double hold_constraint,
                         const std::vector<std::pair<std::string, double>>&
                             buffers) {
  std::string text = R"LIB(library (eos_flow_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;

  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.001;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("HOLD"); }
        fall_constraint (scalar) { values ("HOLD"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }
)LIB";
  for (int i = 0; i < 2; ++i) {
    const std::size_t at = text.find("HOLD");
    text.replace(at, 4, std::to_string(hold_constraint));
  }
  for (const auto& buffer : buffers) {
    text += "  cell (" + buffer.first + ") {\n"
            "    area : 2.0;\n"
            "    pin (A) {\n"
            "      direction : input;\n"
            "      capacitance : 0.001;\n"
            "    }\n"
            "    pin (Y) {\n"
            "      direction : output;\n"
            "      function : \"A\";\n"
            "      timing () {\n"
            "        related_pin : \"A\";\n"
            "        timing_sense : positive_unate;\n"
            "        cell_rise (scalar) { values (\"" +
            std::to_string(buffer.second) +
            "\"); }\n"
            "        cell_fall (scalar) { values (\"" +
            std::to_string(buffer.second) +
            "\"); }\n"
            "        rise_transition (scalar) { values (\"0.05\"); }\n"
            "        fall_transition (scalar) { values (\"0.05\"); }\n"
            "      }\n"
            "    }\n"
            "  }\n";
  }
  text += "}\n";
  return text;
}


const char kFanoutLiberty[] = R"LIB(library (eos_fanout_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;

  lu_table_template (del_cap) {
    variable_1 : total_output_net_capacitance;
    index_1 ("0.005, 0.08");
  }

  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.01;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("0.05"); }
        fall_constraint (scalar) { values ("0.05"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (del_cap) { values ("0.2, 1.8"); }
        cell_fall (del_cap) { values ("0.2, 1.8"); }
        rise_transition (del_cap) { values ("0.05, 0.1"); }
        fall_transition (del_cap) { values ("0.05, 0.1"); }
      }
    }
  }

  cell (BUF_X4) {
    area : 4.0;
    pin (A) {
      direction : input;
      capacitance : 0.005;
    }
    pin (Y) {
      direction : output;
      function : "A";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (del_cap) { values ("0.1, 0.25"); }
        cell_fall (del_cap) { values ("0.1, 0.25"); }
        rise_transition (del_cap) { values ("0.02, 0.05"); }
        fall_transition (del_cap) { values ("0.02, 0.05"); }
      }
    }
  }
}
)LIB";

const char kResynthLef[] = R"LEF(VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.005 ;
SITE core
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END core
LAYER metal1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 0.19 ;
  WIDTH 0.065 ;
END metal1
MACRO DFF
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN D
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END D
  PIN CK
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END CK
  PIN Q
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Q
END DFF
MACRO INV_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END INV_X1
MACRO AND2_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN B
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END B
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END AND2_X1
MACRO OR2_X1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 0.46 BY 2.72 ;
  SITE core ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END A
  PIN B
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END B
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER metal1 ;
        RECT -0.05 -0.05 0.05 0.05 ;
    END
  END Y
END OR2_X1
END LIBRARY
)LEF";

const char kResynthDef[] = R"DEF(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN top ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 5 ;
- r1 DFF + PLACED ( 1000 1000 ) N ;
- u1 AND2_X1 + PLACED ( 2000 1000 ) N ;
- u2 INV_X1 + PLACED ( 3000 1000 ) N ;
- u3 INV_X1 + PLACED ( 4000 1000 ) N ;
- r2 DFF + PLACED ( 5000 1000 ) N ;
END COMPONENTS
PINS 4 ;
- clk + NET clk + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 5000 ) N ;
- d + NET d + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 6000 ) N ;
- b + NET nb + DIRECTION INPUT + USE SIGNAL + PLACED ( 0 7000 ) N ;
- q + NET q + DIRECTION OUTPUT + USE SIGNAL + PLACED ( 10000 5000 ) N ;
END PINS
NETS 8 ;
- clk ( PIN clk ) ( r1 CK ) ( r2 CK ) ;
- d ( PIN d ) ( r1 D ) ;
- nb ( PIN b ) ( u1 B ) ;
- n1 ( r1 Q ) ( u1 A ) ;
- n2 ( u1 Y ) ( u2 A ) ;
- n3 ( u2 Y ) ( u3 A ) ;
- n4 ( u3 Y ) ( r2 D ) ;
- q ( r2 Q ) ( PIN q ) ;
END NETS
END DESIGN
)DEF";

const char kResynthLiberty[] = R"LIB(library (eos_resynth_fixture) {
  delay_model : "table_lookup";
  time_unit : "1ns";
  voltage_unit : "1v";
  capacitive_load_unit (1,pF);
  current_unit : "1mA";
  pulling_resistance_unit : "1kohm";
  leakage_power_unit : "1pW";
  input_threshold_pct_rise : 50;
  input_threshold_pct_fall : 50;
  output_threshold_pct_rise : 50;
  output_threshold_pct_fall : 50;
  slew_lower_threshold_pct_rise : 30;
  slew_lower_threshold_pct_fall : 30;
  slew_upper_threshold_pct_rise : 70;
  slew_upper_threshold_pct_fall : 70;
  slew_derate_from_library : 1.0;
  nom_process : 1.0;
  nom_temperature : 25.0;
  nom_voltage : 1.0;

  cell (DFF) {
    area : 10.0;
    ff (IQ, IQN) {
      next_state : "D";
      clocked_on : "CK";
    }
    pin (D) {
      direction : input;
      capacitance : 0.001;
      timing () {
        related_pin : "CK";
        timing_type : setup_rising;
        rise_constraint (scalar) { values ("0.1"); }
        fall_constraint (scalar) { values ("0.1"); }
      }
      timing () {
        related_pin : "CK";
        timing_type : hold_rising;
        rise_constraint (scalar) { values ("0.05"); }
        fall_constraint (scalar) { values ("0.05"); }
      }
    }
    pin (CK) {
      direction : input;
      capacitance : 0.001;
      clock : true;
    }
    pin (Q) {
      direction : output;
      function : "IQ";
      timing () {
        related_pin : "CK";
        timing_type : rising_edge;
        cell_rise (scalar) { values ("0.2"); }
        cell_fall (scalar) { values ("0.2"); }
        rise_transition (scalar) { values ("0.1"); }
        fall_transition (scalar) { values ("0.1"); }
      }
    }
  }

  cell (INV_X1) {
    area : 1.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "!A";
      timing () {
        related_pin : "A";
        timing_sense : negative_unate;
        cell_rise (scalar) { values ("0.5"); }
        cell_fall (scalar) { values ("0.5"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }

  cell (AND2_X1) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (B) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A & B";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
      timing () {
        related_pin : "B";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }

  cell (OR2_X1) {
    area : 2.0;
    pin (A) {
      direction : input;
      capacitance : 0.001;
    }
    pin (B) {
      direction : input;
      capacitance : 0.001;
    }
    pin (Y) {
      direction : output;
      function : "A | B";
      timing () {
        related_pin : "A";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
      timing () {
        related_pin : "B";
        timing_sense : positive_unate;
        cell_rise (scalar) { values ("0.6"); }
        cell_fall (scalar) { values ("0.6"); }
        rise_transition (scalar) { values ("0.05"); }
        fall_transition (scalar) { values ("0.05"); }
      }
    }
  }
}
)LIB";

const char kCorruptedReplacement[] = R"V(module resynth_cone (n1, nb, n4);
  input n1, nb;
  output n4;
  OR2_X1 u1 (.A(n1), .B(nb), .Y(n4));
endmodule
)V";


class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_phase2_qualification_" + std::to_string(counter_++))) {
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  std::string Write(const std::string& name, const std::string& text) const {
    const std::filesystem::path file = path_ / name;
    std::ofstream out(file);
    out << text;
    out.close();
    return file.string();
  }
  std::string Path(const std::string& name) const {
    return (path_ / name).string();
  }

 private:
  static inline int counter_ = 0;
  std::filesystem::path path_;
};

analysis::SceneAnalysis EstimateScene(db::ScenarioId id,
                                      const std::string& name,
                                      const std::string& corner,
                                      const std::string& liberty) {
  analysis::SceneAnalysis scene;
  scene.scenario.id = id;
  scene.scenario.name = name;
  scene.scenario.mode = "func";
  scene.scenario.corner = corner;
  scene.scenario.parasitics = db::ParasiticsLevel::kEstimate;
  scene.liberty_path = liberty;
  return scene;
}

void AddScenarios(Project& project,
                  const std::vector<analysis::SceneAnalysis>& scenes) {
  for (const analysis::SceneAnalysis& scene : scenes) {
    ASSERT_TRUE(project.scenarios
                    .Create(scene.scenario.name, scene.scenario.mode,
                            scene.scenario.corner, scene.scenario.parasitics)
                    .ok());
  }
}

double Metric(const analysis::SceneResult& scene, const std::string& name) {
  return scene.metrics.at(name).value;
}

double WorstSlack(const analysis::SceneResult& scene) {
  return Metric(scene, "worst_slack_ns");
}

void LoadChain(db::OdbDatabase& db, const TempDir& dir,
               const std::string& buffer_master) {
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", ChainDef(buffer_master))).ok());
}

// Every net's connections as one sorted text: the design's
// connectivity, comparable before and after an edit.
std::string Connectivity(db::OdbDatabase& db) {
  std::vector<std::string> lines;
  for (const std::string& net : db.NetNames()) {
    for (const db::OdbNetConnection& connection : db.NetConnections(net)) {
      lines.push_back(net + " " + connection.instance + "." + connection.pin);
    }
  }
  std::sort(lines.begin(), lines.end());
  std::string text;
  for (const std::string& line : lines) text += line + "\n";
  return text;
}

void LoadResynthFixture(db::OdbDatabase& db, const TempDir& dir) {
  ASSERT_TRUE(db.ReadLef(dir.Write("resynth.lef", kResynthLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("resynth.def", kResynthDef)).ok());
}

eco::ResynthesisRequest ResynthRequestFor(const TempDir& dir) {
  eco::ResynthesisRequest request;
  request.target_net = "n4";
  request.sequential_masters = {"DFF"};
  request.liberty_path = dir.Write("resynth.lib", kResynthLiberty);
  request.scratch_dir = dir.Path(".");
  return request;
}

double ResynthWorstSlack(db::OdbDatabase& db, const TempDir& dir) {
  analysis::SceneAnalysis scene;
  scene.scenario.id = 1;
  scene.scenario.name = "func_tt";
  scene.scenario.mode = "func";
  scene.scenario.corner = "tt";
  scene.scenario.parasitics = db::ParasiticsLevel::kEstimate;
  scene.liberty_path = dir.Write("resynth.lib", kResynthLiberty);
  analysis::TimingService service;
  const auto results = service.Analyze(
      db, dir.Write("resynth.sdc",
                    "create_clock -name clk -period 1.5 [get_ports clk]\n"),
      {scene});
  EXPECT_TRUE(results.ok()) << results.status().message();
  if (!results.ok()) return 0.0;
  return results.value()[0].metrics.at("worst_slack_ns").value;
}

const ReportBinding* ClosureBinding(Project& project) {
  ReportRegistry reports;
  return reports.FindByName(project, "timing_closure");
}

ProofStatus ClosureProof(Project& project) {
  const ReportBinding* binding = ClosureBinding(project);
  if (binding == nullptr) return ProofStatus::kUnknown;
  const session::ArtifactRecord* artifact =
      project.ledger.FindArtifact(binding->artifact_id);
  if (artifact == nullptr) return ProofStatus::kUnknown;
  return artifact->proof;
}

TEST(Phase2Qualification, SizingImprovesEveryRequiredSceneAndCloses) {
  TempDir dir;
  db::OdbDatabase db;
  LoadChain(db, dir, "BUF_X1");
  const std::string connectivity_before = Connectivity(db);
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  // Two required scenes, both violating at the start by the
  // fixtures' construction (setup slack is 0.7 minus the buffer
  // delay): func_tt at -0.1 ns and func_ff at -0.3 ns. The one
  // sizing step improves both and regresses neither (hold slack
  // is consumed in both scenes but stays well above zero).
  request.scenes = {
      EstimateScene(1, "func_tt", "tt",
                    dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 0.8},
                                                            {"BUF_X2", 0.2}}))),
      EstimateScene(2, "func_ff", "ff",
                    dir.Write("ff.lib", ChainLiberty(0.05, {{"BUF_X1", 1.0},
                                                            {"BUF_X2", 0.4}})))};
  request.max_iterations = 5;
  request.sizing_families = {{"BUF_X1", "BUF_X2"}};
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().closed) << outcome.value().stop_reason;
  ASSERT_EQ(outcome.value().steps.size(), 1u);
  EXPECT_EQ(outcome.value().steps[0].label, "size u1 BUF_X1 -> BUF_X2");
  ASSERT_EQ(outcome.value().initial.size(), 2u);
  ASSERT_EQ(outcome.value().final.size(), 2u);
  EXPECT_NEAR(WorstSlack(outcome.value().initial[0]), -0.1, 1e-6);
  EXPECT_NEAR(WorstSlack(outcome.value().initial[1]), -0.3, 1e-6);
  EXPECT_NEAR(WorstSlack(outcome.value().final[0]), 0.5, 1e-6);
  EXPECT_NEAR(WorstSlack(outcome.value().final[1]), 0.3, 1e-6);
  // Improvement is per-scene, not aggregate: every required
  // scene's worst slack and TNS improved, and the gate holds
  // over the whole set.
  for (std::size_t i = 0; i < 2; ++i) {
    EXPECT_GT(WorstSlack(outcome.value().final[i]),
              WorstSlack(outcome.value().initial[i]));
    EXPECT_GT(Metric(outcome.value().final[i], "total_negative_slack_ns"),
              Metric(outcome.value().initial[i], "total_negative_slack_ns"));
  }
  EXPECT_TRUE(TimingGateMet(outcome.value().final));
  // Function preservation for a sizing repair: the swap is the
  // same cell at a stronger drive (identical signal pin set, as
  // the adapter requires), and every net's connections are
  // exactly what they were before the repair.
  EXPECT_EQ(Connectivity(db), connectivity_before);
  EXPECT_EQ(db.InstanceCount(), 3u);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X2");
  // The record: one ledger transaction carrying both scenes'
  // flattened metrics, and the closure binding at proof pass.
  EXPECT_EQ(project.ledger.current_revision(), 1u);
  ASSERT_EQ(project.ledger.history().size(), 1u);
  EXPECT_NEAR(project.ledger.history()[0].after_metrics
                  .at("func_ff/worst_slack_ns")
                  .value,
              0.3, 1e-6);
  ASSERT_NE(ClosureBinding(project), nullptr);
  EXPECT_EQ(ClosureProof(project), ProofStatus::kPass);
}

TEST(Phase2Qualification, CrossSceneRegressionIsRefusedByTheAllSceneRule) {
  TempDir dir;
  db::OdbDatabase db;
  LoadChain(db, dir, "BUF_X1");
  const std::string connectivity_before = Connectivity(db);
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {
      EstimateScene(1, "func_tt", "tt",
                    dir.Write("tt.lib", ChainLiberty(0.05, {{"BUF_X1", 0.8},
                                                            {"BUF_X2", 0.2}}))),
      // The hold-tight corner: the swap that fixes func_tt setup
      // drops this scene's hold slack from +0.15 ns to -0.10 ns.
      // A single-scene judge would take it; the all-scene rule
      // must refuse it.
      EstimateScene(2, "func_ff", "ff",
                    dir.Write("ff.lib", ChainLiberty(0.35, {{"BUF_X1", 0.3},
                                                            {"BUF_X2", 0.05}})))};
  request.max_iterations = 5;
  request.sizing_families = {{"BUF_X1", "BUF_X2"}};
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_FALSE(outcome.value().closed);
  EXPECT_TRUE(outcome.value().steps.empty());
  EXPECT_EQ(outcome.value().candidates_tried, 1u);
  EXPECT_EQ(outcome.value().candidates_rejected, 1u);
  EXPECT_NE(outcome.value().stop_reason.find("worst_hold_slack_ns"),
            std::string::npos)
      << outcome.value().stop_reason;
  // Design and ledger as if the loop never ran an edit.
  EXPECT_EQ(Connectivity(db), connectivity_before);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "BUF_X1");
  EXPECT_EQ(project.ledger.current_revision(), 0u);
  EXPECT_TRUE(project.ledger.history().empty());
  // The run is still on record, at proof unknown: a loop that
  // did not close timing certifies nothing.
  ASSERT_NE(ClosureBinding(project), nullptr);
  EXPECT_EQ(ClosureProof(project), ProofStatus::kUnknown);
}

TEST(Phase2Qualification, BufferingImprovesTheHighFanoutFixture) {
  TempDir dir;
  db::OdbDatabase db;
  ASSERT_TRUE(db.ReadLef(dir.Write("cells.lef", kLef)).ok());
  ASSERT_TRUE(db.ReadDef(dir.Write("top.def", kFanoutDef)).ok());
  CloseTimingRequest request;
  request.sdc_path =
      dir.Write("top.sdc", "create_clock -name clk -period 1 [get_ports clk]\n");
  request.scenes = {EstimateScene(1, "func_tt", "tt",
                                  dir.Write("tt.lib", kFanoutLiberty))};
  request.max_iterations = 3;
  request.buffer_masters = {"BUF_X4"};
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().closed) << outcome.value().stop_reason;
  ASSERT_EQ(outcome.value().steps.size(), 1u);
  EXPECT_NE(outcome.value().steps[0].label.find("buffer net wide"),
            std::string::npos);
  EXPECT_NEAR(WorstSlack(outcome.value().initial[0]), -0.9, 0.01);
  EXPECT_NEAR(WorstSlack(outcome.value().final[0]), 0.45, 0.01);
  EXPECT_EQ(db.InstanceCount(), 10u);
  EXPECT_EQ(project.ledger.current_revision(), 1u);
  EXPECT_EQ(ClosureProof(project), ProofStatus::kPass);
}

TEST(Phase2Qualification, ResynthesisStandsOnlyOnAnEquivalenceProof) {
  TempDir dir;
  db::OdbDatabase db;
  LoadResynthFixture(db, dir);
  EXPECT_NEAR(ResynthWorstSlack(db, dir), -0.4, 0.01);
  const auto outcome = eco::ResynthesizeCone(db, ResynthRequestFor(dir));
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  // The edit stands because the proof of the full design (the
  // registers cut at their boundaries) concluded equivalent:
  // that verdict, recorded in the outcome, is the function
  // preservation evidence for a logic-changing repair.
  EXPECT_TRUE(outcome.value().applied) << outcome.value().reason;
  EXPECT_EQ(outcome.value().proof, eco::ProofVerdict::kEquivalent);
  EXPECT_EQ(outcome.value().cone_instances,
            std::vector<std::string>({"u1", "u2", "u3"}));
  ASSERT_EQ(outcome.value().created_instances.size(), 1u);
  EXPECT_EQ(db.InstanceCount(), 3u);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance(outcome.value().created_instances[0], &info));
  EXPECT_EQ(info.master, "AND2_X1");
  EXPECT_NEAR(ResynthWorstSlack(db, dir), 0.6, 0.01);
}

TEST(Phase2Qualification, CorruptedSpliceIsProvedNotEquivalentAndRolledBack) {
  TempDir dir;
  db::OdbDatabase db;
  LoadResynthFixture(db, dir);
  const std::string connectivity_before = Connectivity(db);
  const std::string replacement =
      dir.Write("corrupted_cone.v", kCorruptedReplacement);
  const auto outcome =
      eco::SpliceReplacementCone(db, ResynthRequestFor(dir), replacement);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  // The replacement computes OR where the cone computes AND:
  // the proof must say not-equivalent, the splice must not
  // stand, and the design must be exactly as it was, in
  // instances, masters, connections and timing alike.
  EXPECT_FALSE(outcome.value().applied);
  EXPECT_EQ(outcome.value().proof, eco::ProofVerdict::kNotEquivalent);
  EXPECT_TRUE(outcome.value().created_instances.empty());
  EXPECT_EQ(db.InstanceCount(), 5u);
  EXPECT_EQ(Connectivity(db), connectivity_before);
  db::OdbInstanceInfo info;
  ASSERT_TRUE(db.FindInstance("u1", &info));
  EXPECT_EQ(info.master, "AND2_X1");
  ASSERT_TRUE(db.FindInstance("u3", &info));
  EXPECT_EQ(info.master, "INV_X1");
  EXPECT_NEAR(ResynthWorstSlack(db, dir), -0.4, 0.01);
}

TEST(Phase2Qualification, ResynthesisThroughTheLoopPreservesFunctionAndCloses) {
  TempDir dir;
  db::OdbDatabase db;
  LoadResynthFixture(db, dir);
  const std::string liberty = dir.Write("resynth.lib", kResynthLiberty);
  CloseTimingRequest request;
  request.sdc_path = dir.Write(
      "top.sdc", "create_clock -name clk -period 1.5 [get_ports clk]\n");
  request.scenes = {EstimateScene(1, "func_tt", "tt", liberty)};
  request.max_iterations = 3;
  // No sizing families and no buffer masters: the only
  // candidates are resynthesis candidates, each screened
  // through the ECO protocol with the adapter's mandatory
  // equivalence proof inside the edit.
  request.resynthesis.enabled = true;
  request.resynthesis.liberty_path = liberty;
  request.resynthesis.sequential_masters = {"DFF"};
  request.resynthesis.scratch_dir = dir.Path(".");
  Project project;
  AddScenarios(project, request.scenes);

  const auto outcome = RunTimingClosure(project, db, request);
  ASSERT_TRUE(outcome.ok()) << outcome.status().message();
  EXPECT_TRUE(outcome.value().closed) << outcome.value().stop_reason;
  ASSERT_EQ(outcome.value().steps.size(), 1u);
  EXPECT_NE(outcome.value().steps[0].label.find("resynthesize cone"),
            std::string::npos);
  EXPECT_NEAR(WorstSlack(outcome.value().initial[0]), -0.4, 0.01);
  EXPECT_NEAR(WorstSlack(outcome.value().final[0]), 0.6, 0.01);
  EXPECT_EQ(db.InstanceCount(), 3u);
  EXPECT_EQ(project.ledger.current_revision(), 1u);
  ASSERT_NE(ClosureBinding(project), nullptr);
  EXPECT_EQ(ClosureProof(project), ProofStatus::kPass);
}

}  // namespace
}  // namespace eos
