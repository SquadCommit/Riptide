/**
 * @author Alexander Breuer (alex.breuer AT uni-jena.de)
 * @author Yannik Köllmann (yannik.koellmann AT uni-jena.de)
 * @author Jan Vogt (jan.vogt AT uni-jena.de)
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Entry-point for simulations.
 **/
#include "io/csv/Csv.h"
#include "io/netcdf/NetCDF.h"
#include "io/stations/Stations.h"
#include "patches/WavePropagation1d/WavePropagation1d.h"
#include "patches/WavePropagation2d/WavePropagation2d.h"
#include "setups/artificialtsunami2d/ArtificialTsunami2d.h"
#include "setups/checkpoint/CheckPoint.h"
#include "setups/dambreak/CircularDamBreak2d.h"
#include "setups/dambreak/DamBreak1d.h"
#include "setups/rarerare/RareRare1d.h"
#include "setups/shockshock/ShockShock1d.h"
#include "setups/subcritical1d/SubCritical1d.h"
#include "setups/supercritical1d/SuperCritical1d.h"
#include "setups/tsunamievent1d/TsunamiEvent1d.h"
#include "setups/tsunamievent2d/TsunamiEvent2d.h"
#include "util/OmpDefaults.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <sys/stat.h>

namespace {
std::atomic<bool> g_interrupted{false};
void onSigint(int) { g_interrupted.store(true); }
} // namespace

static void printUsage(const char* i_prog) {
  std::cerr << "usage: " << i_prog
            << " -n N_CELLS -s SOLVER -p SETUP [PARAMS...] [-d DOMAIN_SIZE] "
               "[-t END_TIME]"
            << std::endl;
  std::cerr << std::endl;
  std::cerr << "mandatory parameters:" << std::endl;
  std::cerr << "  -n   number of cells in each-direction (>= 1)" << std::endl;
  std::cerr << "  -p   setup with configuration:" << std::endl;
  std::cerr
      << "        > DamBreak         <hLeft> <hRight> <location> [huLeft=0] "
         "[huRight=0]"
      << std::endl;
  std::cerr << "        > RareRare         <height> <momentum> <location>"
            << std::endl;
  std::cerr << "        > ShockShock       <height> <momentum> <location> "
               "[amplitude=0] [center=0] [width=0]"
            << std::endl;
  std::cerr << "        >   (use amplitude!=0 for bathymetry variant)"
            << std::endl;
  std::cerr << "        > SubCritical" << std::endl;
  std::cerr << "        > SuperCritical" << std::endl;
  std::cerr << "        > TsunamiEvent    <bathymetry.csv>" << std::endl;
  std::cerr << "        > DamBreak2d      [hInner=10] [hOuter=5] [centerX=5] "
               "[centerY=5] [radius=2]"
            << std::endl;
  std::cerr << "        >                 [obstacleAmp=0] [obstacleCX=0] "
               "[obstacleCY=0] [obstacleW=0]"
            << std::endl;
  std::cerr << "        > ArtificialTsunami2d  (no params; 100m pool + "
               "Gaussian displacement)"
            << std::endl;
  std::cerr << "        > TsunamiEvent2d  <bath.nc> <displ.nc>  (domain "
               "derived from bathymetry file)"
            << std::endl;
  std::cerr << std::endl;
  std::cerr << "optional parameters:" << std::endl;
  std::cerr << "  -s         solver:  FWave | Roe (default: FWave)"
            << std::endl;
  std::cerr << "  -c         path to XML config file (optional, for stations)"
            << std::endl;
  std::cerr << "  -d         total domain size in meters (default: 10.0)"
            << std::endl;
  std::cerr << "  -t         simulation end time in seconds (default: 1.25)"
            << std::endl;
  std::cerr << "  --bc-left  left boundary condition: outflow | reflecting "
               "(default: outflow)"
            << std::endl;
  std::cerr << "  --bc-right right boundary condition: outflow | reflecting "
               "(default: outflow)"
            << std::endl;
  std::cerr << "  -o, --out  output directory (default: auto-derived from "
               "setup + parameters)"
            << std::endl;
  std::cerr << "  -k         coarsening factor: k×k cells averaged into one "
               "output cell (default: 1)"
            << std::endl;
  std::cerr << "  --io-steps file-output interval in time steps; 0 disables "
               "file output (default: derived from end time)"
            << std::endl;
  std::cerr << std::endl;
  std::cerr << "checkpointing:" << std::endl;
  std::cerr << "  If <out>/solution.nc already contains at least one time "
               "step, the run continues from that checkpoint and the -p "
               "argument is ignored."
            << std::endl;
  std::cerr << std::endl;
  std::cerr << "examples:" << std::endl;
  std::cerr << "  " << i_prog << " -n 100 -p DamBreak 10 5 5" << std::endl;
  std::cerr << "  " << i_prog << " -n 100 -t 5 -p DamBreak2d" << std::endl;
  std::cerr << "  " << i_prog
            << " -n 2500 -s Roe -d 25000 -t 7200 -p DamBreak 14 3.5 12500 0 0.7"
            << std::endl;
  std::cerr << "  " << i_prog
            << " -n 100 -d 10 -t 1.25 --bc-right reflecting -p ShockShock 10 "
               "5 11"
            << std::endl;
  std::cerr << "  " << i_prog
            << " -n 100 -d 25 -t 200 -p ShockShock 10 5 10 3 5 1" << std::endl;
  std::cerr << "        (ShockShock with bathymetric hump: amplitude=3, "
               "center=5, width=1)"
            << std::endl;
}

int main(int i_argc, char* i_argv[]) {
  tsunami_lab::util::applySaneOmpScheduleDefault();

  // graceful CTRL-C handling: ask the time loop to stop so destructors run
  // (in particular nc_close finalizes the NetCDF/HDF5 checkpoint file).
  std::signal(SIGINT, onSigint);
  std::signal(SIGTERM, onSigint);

  tsunami_lab::t_idx l_nx = 0;
  tsunami_lab::t_idx l_ny = 1;
  bool l_is2d = false;
  tsunami_lab::t_real l_domainSize = 10.0;
  bool l_domainSizeSet = false;
  tsunami_lab::t_real l_domainOrigin = 0.0;  // left boundary coordinate
  tsunami_lab::t_real l_domainOriginY = 0.0; // bottom boundary coordinate
  tsunami_lab::t_real l_endTime = 1.25;
  std::string l_solverMode = "fwave";
  std::string l_setupMode;
  std::string l_configPath;
  std::string l_outDirOverride;
  tsunami_lab::t_idx l_coarseK = 1;
  // file-output interval in time steps. -1 keeps the default (derived from the
  // end time); 0 disables file output entirely
  long l_ioSteps = -1;

  // boundary conditions default to outflow on both sides
  tsunami_lab::patches::BoundaryCondition l_bcLeft =
      tsunami_lab::patches::BoundaryCondition::Outflow;
  tsunami_lab::patches::BoundaryCondition l_bcRight =
      tsunami_lab::patches::BoundaryCondition::Outflow;

  tsunami_lab::t_real l_p1 = 0, l_p2 = 0, l_p3 = 0, l_p4 = 0, l_p5 = 0,
                      l_p6 = 0;

  tsunami_lab::setups::Setup* l_setup = nullptr;
  tsunami_lab::setups::TsunamiEvent2d* l_tsunamiEvent2d = nullptr;

  auto l_parseBc = [&](const std::string& i_val,
                       tsunami_lab::patches::BoundaryCondition& o_bc) -> bool {
    std::string l_v = i_val;
    std::transform(l_v.begin(), l_v.end(), l_v.begin(), ::tolower);
    if (l_v == "outflow") {
      o_bc = tsunami_lab::patches::BoundaryCondition::Outflow;
      return true;
    }
    if (l_v == "reflecting") {
      o_bc = tsunami_lab::patches::BoundaryCondition::Reflecting;
      return true;
    }
    return false;
  };

  std::cout << "####################################" << std::endl;
  std::cout << "### Tsunami Lab                  ###" << std::endl;
  std::cout << "###                              ###" << std::endl;
  std::cout << "### https://scalable.uni-jena.de ###" << std::endl;
  std::cout << "####################################" << std::endl;

  // argument parsing
  for (int l_i = 1; l_i < i_argc; ++l_i) {
    std::string l_arg = i_argv[l_i];

    if (l_arg == "-h" || l_arg == "--help") {
      printUsage(i_argv[0]);
      return EXIT_SUCCESS;
    } else if ((l_arg == "-n" || l_arg == "--cells") && l_i + 1 < i_argc) {
      long l_val = std::atol(i_argv[++l_i]);
      if (l_val < 1) {
        std::cerr << "error: -n must be >= 1" << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }
      l_nx = static_cast<tsunami_lab::t_idx>(l_val);
      l_ny = static_cast<tsunami_lab::t_idx>(l_val);

    } else if ((l_arg == "-s" || l_arg == "--solver") && l_i + 1 < i_argc) {
      l_solverMode = i_argv[++l_i];
      std::transform(l_solverMode.begin(), l_solverMode.end(),
                     l_solverMode.begin(), ::tolower);
      if (l_solverMode != "fwave" && l_solverMode != "roe") {
        std::cerr << "error: unknown solver '" << l_solverMode
                  << "' -- use FWave or Roe" << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }

    } else if ((l_arg == "-c" || l_arg == "--config") && l_i + 1 < i_argc) {
      l_configPath = i_argv[++l_i];

    } else if ((l_arg == "-o" || l_arg == "--out") && l_i + 1 < i_argc) {
      l_outDirOverride = i_argv[++l_i];

    } else if ((l_arg == "-k" || l_arg == "--coarse") && l_i + 1 < i_argc) {
      long l_val = std::atol(i_argv[++l_i]);
      if (l_val < 1) {
        std::cerr << "error: -k must be >= 1" << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }
      l_coarseK = static_cast<tsunami_lab::t_idx>(l_val);

    } else if (l_arg == "--io-steps" && l_i + 1 < i_argc) {
      l_ioSteps = std::atol(i_argv[++l_i]);
      if (l_ioSteps < 0) {
        std::cerr << "error: --io-steps must be >= 0 (0 disables file output)"
                  << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }

    } else if ((l_arg == "-d" || l_arg == "--domain") && l_i + 1 < i_argc) {
      l_domainSize = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
      l_domainSizeSet = true;
      if (l_domainSize <= 0) {
        std::cerr << "error: -d (domain size) must be > 0" << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }

    } else if ((l_arg == "-t" || l_arg == "--time") && l_i + 1 < i_argc) {
      l_endTime = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
      if (l_endTime <= 0) {
        std::cerr << "error: -t (end time) must be > 0" << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }

    } else if (l_arg == "--bc-left" && l_i + 1 < i_argc) {
      if (!l_parseBc(i_argv[++l_i], l_bcLeft)) {
        std::cerr << "error: --bc-left must be 'outflow' or 'reflecting'"
                  << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }

    } else if (l_arg == "--bc-right" && l_i + 1 < i_argc) {
      if (!l_parseBc(i_argv[++l_i], l_bcRight)) {
        std::cerr << "error: --bc-right must be 'outflow' or 'reflecting'"
                  << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }

    } else if ((l_arg == "-p" || l_arg == "--problem") && l_i + 1 < i_argc) {
      l_setupMode = i_argv[++l_i];
      std::transform(l_setupMode.begin(), l_setupMode.end(),
                     l_setupMode.begin(), ::tolower);

      if (l_setupMode == "dambreak") {
        if (l_i + 3 >= i_argc) {
          std::cerr << "error: DamBreak requires at least 3 parameters: "
                       "<hLeft> <hRight> <location>"
                    << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }
        l_p1 =
            static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i])); // hLeft
        l_p2 = static_cast<tsunami_lab::t_real>(
            std::atof(i_argv[++l_i])); // hRight
        l_p3 = static_cast<tsunami_lab::t_real>(
            std::atof(i_argv[++l_i])); // location

        if (l_p1 <= 0 || l_p2 <= 0) {
          std::cerr << "error: DamBreak water heights must be > 0" << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }

        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-') {
          l_p4 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        }
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-') {
          l_p5 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        }
        l_setup =
            new tsunami_lab::setups::DamBreak1d(l_p1, l_p4, l_p2, l_p5, l_p3);

      } else if (l_setupMode == "dambreak2d") {
        l_is2d = true;
        // optional params: hInner hOuter centerX centerY radius obstacleAmp
        // obstacleCX obstacleCY obstacleW
        tsunami_lab::t_real l_hInner = 10.0f, l_hOuter = 5.0f;
        tsunami_lab::t_real l_cx = 0.0f, l_cy = 0.0f, l_r = 10.0f;
        if (!l_domainSizeSet) {
          l_domainSize = 100.0f;
        }
        l_domainOrigin = -0.5f * l_domainSize;
        l_domainOriginY = l_domainOrigin;
        tsunami_lab::t_real l_oAmp = 0.0f, l_oCx = 0.0f, l_oCy = 0.0f,
                            l_oW = 0.0f;
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_hInner = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_hOuter = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_cx = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_cy = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_r = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_oAmp = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_oCx = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_oCy = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-')
          l_oW = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        l_setup = new tsunami_lab::setups::CircularDamBreak2d(
            l_hInner, l_hOuter, l_cx, l_cy, l_r, l_oAmp, l_oCx, l_oCy, l_oW);

      } else if (l_setupMode == "rarerare") {
        if (l_i + 3 >= i_argc) {
          std::cerr << "error: RareRare requires 3 parameters: "
                       "<height> <momentum> <location>"
                    << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }
        l_p1 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        l_p2 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        l_p3 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));

        if (l_p1 <= 0) {
          std::cerr << "error: RareRare water height must be > 0" << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }

        l_setup = new tsunami_lab::setups::RareRare1d(l_p1, l_p2, l_p3);

      } else if (l_setupMode == "shockshock") {
        if (l_i + 3 >= i_argc) {
          std::cerr << "error: ShockShock requires 3 parameters: "
                       "<height> <momentum> <location>"
                    << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }
        l_p1 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        l_p2 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        l_p3 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));

        if (l_p1 <= 0) {
          std::cerr << "error: ShockShock water height must be > 0"
                    << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }

        // amplitude (optional)
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-') {
          l_p4 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        }

        // center (optional, only if amplitude was provided)
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-') {
          l_p5 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        }

        // width (optional, only if amplitude was provided)
        if (l_i + 1 < i_argc && i_argv[l_i + 1][0] != '-') {
          l_p6 = static_cast<tsunami_lab::t_real>(std::atof(i_argv[++l_i]));
        }

        l_setup = new tsunami_lab::setups::ShockShock1d(l_p1, l_p2, l_p3, l_p4,
                                                        l_p5, l_p6);

      } else if (l_setupMode == "subcritical") {
        l_setup = new tsunami_lab::setups::SubCritical1d();
      } else if (l_setupMode == "supercritical") {
        l_setup = new tsunami_lab::setups::SuperCritical1d();
      } else if (l_setupMode == "tsunamievent") {
        if (l_i + 1 >= i_argc) {
          std::cerr << "error: TsunamiEvent requires 1 parameter: "
                       "<bathymetry.csv>"
                    << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }
        const char* l_csvPath = i_argv[++l_i];
        l_setup = new tsunami_lab::setups::TsunamiEvent1d(l_csvPath);
      } else if (l_setupMode == "artificialtsunami2d") {
        l_is2d = true;
        if (!l_domainSizeSet)
          l_domainSize = 200000.0f; // 200 km default
        l_domainOrigin = -l_domainSize / 2.0f;
        l_domainOriginY = l_domainOrigin;
        l_setup = new tsunami_lab::setups::ArtificialTsunami2d();
      } else if (l_setupMode == "tsunamievent2d") {
        if (l_i + 2 >= i_argc) {
          std::cerr << "error: TsunamiEvent2d requires 2 parameters: "
                       "<bath.nc> <displ.nc>"
                    << std::endl;
          printUsage(i_argv[0]);
          return EXIT_FAILURE;
        }
        const char* l_bathPath = i_argv[++l_i];
        const char* l_displPath = i_argv[++l_i];
        l_is2d = true;
        l_tsunamiEvent2d =
            new tsunami_lab::setups::TsunamiEvent2d(l_bathPath, l_displPath);
        l_setup = l_tsunamiEvent2d;
      } else {
        std::cerr << "error: unknown setup '" << l_setupMode
                  << "' -- use DamBreak, RareRare, ShockShock, SubCritical, "
                     "SuperCritical, TsunamiEvent, ArtificialTsunami2d, or "
                     "TsunamiEvent2d"
                  << std::endl;
        printUsage(i_argv[0]);
        return EXIT_FAILURE;
      }

    } else {
      std::cerr << "error: unknown or incomplete argument: " << l_arg
                << std::endl;
      printUsage(i_argv[0]);
      return EXIT_FAILURE;
    }
  }

  // validate required arguments
  if (l_nx == 0) {
    std::cerr << "error: missing required argument -n" << std::endl;
    printUsage(i_argv[0]);
    return EXIT_FAILURE;
  }

  // determine output directory. Deterministic, so a second run with the same
  // command line lands on the same path and can pick up its checkpoint file.
  std::string l_simBaseDir = "simulations/";
  std::string l_simDir;
  if (!l_outDirOverride.empty()) {
    l_simDir = l_outDirOverride;
  } else {
    l_simDir = l_simBaseDir + l_setupMode + "_" +
               std::to_string(static_cast<long long>(l_p1)) + "_" +
               std::to_string(static_cast<long long>(l_p2)) + "_" +
               std::to_string(static_cast<long long>(l_p3)) + "_" +
               l_solverMode + "_n" + std::to_string(l_nx);
  }
  std::string l_ncPath = l_simDir + "/solution.nc";

  // try to resume from an existing checkpoint
  bool l_appendMode = tsunami_lab::io::NetCDF::hasCheckpoint(l_ncPath.c_str());
  tsunami_lab::t_real l_simTimeStart = 0;
  tsunami_lab::t_real l_dtCheckpoint = 0;
  tsunami_lab::setups::CheckPoint* l_checkpointSetup = nullptr;

  if (l_appendMode) {
    std::cout << "\033[33mfound checkpoint at " << l_ncPath
              << " — resuming\033[0m" << std::endl;

    // discard any setup that was constructed from -p; replace with CheckPoint
    delete l_setup;
    l_setup = nullptr;
    l_tsunamiEvent2d = nullptr; // owned via l_setup, already deleted

    l_checkpointSetup = new tsunami_lab::setups::CheckPoint(l_ncPath.c_str());
    l_setup = l_checkpointSetup;
    const auto& l_info = l_checkpointSetup->getInfo();

    // grid + run params from checkpoint always win
    l_is2d = true;
    l_nx = l_info.nx;
    l_ny = l_info.ny;
    l_domainOrigin = l_info.originX;
    l_domainOriginY = l_info.originY;
    l_domainSize = l_info.dxy * static_cast<tsunami_lab::t_real>(l_info.nx);
    if (l_info.endTime > 0)
      l_endTime = l_info.endTime;
    if (l_info.dt > 0)
      l_dtCheckpoint = l_info.dt;
    if (!l_info.solverMode.empty())
      l_solverMode = l_info.solverMode;
    auto l_applyBc = [&](const std::string& i_val,
                         tsunami_lab::patches::BoundaryCondition& o_bc) {
      if (i_val == "reflecting")
        o_bc = tsunami_lab::patches::BoundaryCondition::Reflecting;
      else if (i_val == "outflow")
        o_bc = tsunami_lab::patches::BoundaryCondition::Outflow;
    };
    l_applyBc(l_info.bcLeft, l_bcLeft);
    l_applyBc(l_info.bcRight, l_bcRight);

    l_simTimeStart = l_info.lastSimTime;
    if (l_setupMode.empty())
      l_setupMode = l_info.setupMode;
  } else {
    // require -p only when there is no checkpoint to resume from
    if (l_setup == nullptr) {
      std::cerr << "error: missing required argument -p" << std::endl;
      printUsage(i_argv[0]);
      return EXIT_FAILURE;
    }

    // for TsunamiEvent2d: derive domain from bathymetry file if not overridden
    if (l_tsunamiEvent2d != nullptr && !l_domainSizeSet) {
      l_domainOrigin = l_tsunamiEvent2d->getDomainOriginX();
      l_domainOriginY = l_tsunamiEvent2d->getDomainOriginY();
      l_domainSize = l_tsunamiEvent2d->getDomainSizeX();
      // compute ny so cells stay square (dx = dy)
      tsunami_lab::t_real l_dxy0 =
          l_domainSize / static_cast<tsunami_lab::t_real>(l_nx);
      l_ny = static_cast<tsunami_lab::t_idx>(
          l_tsunamiEvent2d->getDomainSizeY() / l_dxy0 + 0.5f);
      if (l_ny < 1)
        l_ny = 1;
    }
  }

  // derived quantities — computed after all args are parsed
  tsunami_lab::t_real l_dxy =
      l_domainSize / static_cast<tsunami_lab::t_real>(l_nx);

  std::cout << "runtime configuration" << std::endl;
  std::cout << "  number of cells in x-direction: " << l_nx << std::endl;
  std::cout << "  number of cells in y-direction: " << l_ny << std::endl;
  std::cout << "  domain size:                    " << l_domainSize << " m"
            << std::endl;
  std::cout << "  cell size:                      " << l_dxy << " m"
            << std::endl;
  std::cout << "  solver:                         " << l_solverMode
            << std::endl;
  std::cout << "  setup:                          " << l_setupMode << std::endl;
  std::cout << "  end time:                       " << l_endTime << " s"
            << std::endl;
  if (l_coarseK > 1)
    std::cout << "  coarsening factor:              " << l_coarseK << std::endl;
  auto l_bcName = [](tsunami_lab::patches::BoundaryCondition i_bc) {
    return i_bc == tsunami_lab::patches::BoundaryCondition::Reflecting
               ? "reflecting"
               : "outflow";
  };
  std::cout << "  boundary (left / right):        " << l_bcName(l_bcLeft)
            << " / " << l_bcName(l_bcRight) << std::endl;

  // construct solver and initialize cells
  if (!l_is2d)
    l_ny = 1;
  tsunami_lab::patches::WavePropagation* l_waveProp =
      l_is2d ? static_cast<tsunami_lab::patches::WavePropagation*>(
                   new tsunami_lab::patches::WavePropagation2d(l_nx, l_ny))
             : static_cast<tsunami_lab::patches::WavePropagation*>(
                   new tsunami_lab::patches::WavePropagation1d(l_nx));

  // maximum wave speed observed in the initial state: max(|u| + sqrt(g*h))
  tsunami_lab::t_real l_speedMax = 0;

  // set up solver — sample setup at cell centers
  for (tsunami_lab::t_idx l_cy = 0; l_cy < l_ny; l_cy++) {
    tsunami_lab::t_real l_y = l_domainOriginY + (l_cy + 0.5f) * l_dxy;

    for (tsunami_lab::t_idx l_cx = 0; l_cx < l_nx; l_cx++) {
      tsunami_lab::t_real l_x = l_domainOrigin + (l_cx + 0.5f) * l_dxy;

      // get initial values of the setup
      tsunami_lab::t_real l_h = l_setup->getHeight(l_x, l_y);

      tsunami_lab::t_real l_hu = l_setup->getMomentumX(l_x, l_y);
      tsunami_lab::t_real l_hv = l_setup->getMomentumY(l_x, l_y);
      tsunami_lab::t_real l_b = l_setup->getBathymetry(l_x, l_y);

      if (l_h > 0) {
        tsunami_lab::t_real l_u = l_hu / l_h;
        tsunami_lab::t_real l_v = l_hv / l_h;
        tsunami_lab::t_real l_speed = std::max(std::abs(l_u), std::abs(l_v)) +
                                      std::sqrt(tsunami_lab::g * l_h);
        l_speedMax = std::max(l_speedMax, l_speed);
      }

      // set initial values in wave propagation solver
      l_waveProp->setHeight(l_cx, l_cy, l_h);

      l_waveProp->setMomentumX(l_cx, l_cy, l_hu);

      l_waveProp->setMomentumY(l_cx, l_cy, l_hv);

      l_waveProp->setBathymetry(l_cx, l_cy, l_b);
    }
  }

  // derive constant time step. on restart we re-use the dt stored at
  // checkpoint creation so the time-step grid matches; otherwise it's
  // computed from the initial state's maximum wave speed.
  // In 2D the unsplit scheme applies X- and Y-sweeps with the same dt,
  // so the combined CFL is CFL_x + CFL_y. Halving dt keeps CFL_2D = 0.5.
  tsunami_lab::t_real l_dt = 0;
  if (l_appendMode && l_dtCheckpoint > 0) {
    l_dt = l_dtCheckpoint;
  } else {
    tsunami_lab::t_real l_cflFactor = l_is2d ? 0.25f : 0.5f;
    l_dt = l_cflFactor * l_dxy / l_speedMax;
  }

  // derive scaling for a time step
  tsunami_lab::t_real l_scaling = l_dt / l_dxy;

  mkdir(l_simBaseDir.c_str(), 0755);
  if (mkdir(l_simDir.c_str(), 0755) != 0 && errno != EEXIST) {
    std::cerr << "error: failed to create output directory: " << l_simDir
              << std::endl;
    delete l_setup;
    delete l_waveProp;
    return EXIT_FAILURE;
  }

  // NetCDF writer for 2D simulations — append on restart, otherwise create
  tsunami_lab::io::NetCDF* l_netCdf = nullptr;
  if (l_is2d) {
    std::cout << "  netCDF output: " << l_ncPath << std::endl;
    if (l_appendMode) {
      l_netCdf = new tsunami_lab::io::NetCDF(l_ncPath.c_str());
    } else {
      l_netCdf = new tsunami_lab::io::NetCDF(l_nx, l_ny, l_dxy, l_dxy,
                                             l_domainOrigin, l_domainOriginY,
                                             l_ncPath.c_str(), l_coarseK);
      auto l_bcStr = [](tsunami_lab::patches::BoundaryCondition i_bc) {
        return i_bc == tsunami_lab::patches::BoundaryCondition::Reflecting
                   ? std::string("reflecting")
                   : std::string("outflow");
      };
      l_netCdf->writeMetadata(l_endTime, l_dt, l_bcStr(l_bcLeft),
                              l_bcStr(l_bcRight), l_solverMode, l_setupMode);
    }
  }

  // stations — optional, loaded from XML config
  tsunami_lab::io::Stations* l_stations = nullptr;
  if (!l_configPath.empty()) {
    pugi::xml_document l_doc;
    pugi::xml_parse_result l_result = l_doc.load_file(l_configPath.c_str());
    if (!l_result) {
      std::cerr << "warning: could not parse config file '" << l_configPath
                << "': " << l_result.description() << std::endl;
    } else {
      pugi::xml_node l_stationsNode = l_doc.child("config").child("stations");
      if (l_stationsNode) {
        std::string l_stationsDir = l_simDir + "/stations";
        l_stations =
            new tsunami_lab::io::Stations(tsunami_lab::io::Stations::fromXml(
                l_stationsNode, l_domainOrigin, l_domainOriginY, l_dxy, l_dxy,
                l_stationsDir));
        std::cout << "  stations config loaded from: " << l_configPath
                  << std::endl;
      }
    }
  }

  // time loop — start from checkpoint state if resuming
  tsunami_lab::t_idx l_timeStep =
      l_appendMode ? static_cast<tsunami_lab::t_idx>(l_simTimeStart / l_dt) : 0;
  tsunami_lab::t_idx l_nOut = 0;
  tsunami_lab::t_real l_simTime = l_simTimeStart;

  // file-output interval in time steps. By default one snapshot every 0.5 % of
  // the total time steps; overridden by --io-steps (0 disables file output).
  bool l_writeOutput = (l_ioSteps != 0);
  tsunami_lab::t_idx l_outInterval =
      (l_ioSteps > 0) ? static_cast<tsunami_lab::t_idx>(l_ioSteps)
                      : std::max(static_cast<tsunami_lab::t_idx>(25),
                                 static_cast<tsunami_lab::t_idx>(
                                     0.005f * l_endTime / l_dt));

  std::cout << "entering time loop" << std::endl;

  // accumulated wall-clock time spent in the pure compute kernel
  // (setGhost + timeStep). Excludes I/O and setup, per ch. 8 task 1.3.
  std::chrono::nanoseconds l_computeDuration{0};
  tsunami_lab::t_idx l_iterCount = 0;

  // iterate over time
  while (l_simTime < l_endTime) {
    if (g_interrupted.load()) {
      std::cout << "\n\033[33minterrupt received — writing final checkpoint "
                   "and shutting down\033[0m"
                << std::endl;
      if (l_is2d && l_netCdf != nullptr) {
        l_netCdf->write(l_simTime, l_waveProp->getHeight(),
                        l_waveProp->getMomentumX(), l_waveProp->getMomentumY(),
                        l_waveProp->getBathymetry(), l_waveProp->getStride());
      }
      break;
    }
    if (l_writeOutput && l_timeStep % l_outInterval == 0) {
      std::cout << "  simulation time / #time steps: " << l_simTime << " s / "
                << l_timeStep << std::endl;

      if (l_is2d) {
        l_netCdf->write(l_simTime, l_waveProp->getHeight(),
                        l_waveProp->getMomentumX(), l_waveProp->getMomentumY(),
                        l_waveProp->getBathymetry(), l_waveProp->getStride());
      } else {
        std::string l_path =
            l_simDir + "/solution_" +
            std::string(4 - std::min(4, (int)std::to_string(l_nOut).length()),
                        '0') +
            std::to_string(l_nOut) + ".csv";
        std::cout << "  writing wave field to " << l_path << std::endl;

        std::ofstream l_file(l_path);
        tsunami_lab::io::Csv::write(
            l_dxy, l_nx, l_ny, 1, l_waveProp->getHeight(),
            l_waveProp->getBathymetry(), l_waveProp->getMomentumX(), nullptr,
            l_simTime, l_file);
        l_file.close();
      }
      l_nOut++;
    }

    if (l_stations != nullptr) {
      l_stations->write(l_simTime, l_waveProp->getHeight(),
                        l_waveProp->getMomentumX(), l_waveProp->getMomentumY(),
                        l_waveProp->getBathymetry(),
                        l_is2d ? l_waveProp->getStride() : 1);
    }

    auto l_tic = std::chrono::steady_clock::now();
    l_waveProp->setGhost(l_bcLeft, l_bcRight);
    l_waveProp->timeStep(l_scaling, l_solverMode);
    l_computeDuration += std::chrono::steady_clock::now() - l_tic;
    l_iterCount++;

    l_timeStep++;
    l_simTime += l_dt;
  }

  std::cout << "finished time loop" << std::endl;

  // report timing: total compute time, and the normalized
  // "time per cell and iteration" metric.
  double l_computeSec =
      std::chrono::duration<double>(l_computeDuration).count();
  tsunami_lab::t_idx l_nCells = l_nx * l_ny;
  std::cout << "timing (compute kernel only, I/O and setup excluded):"
            << std::endl;
  std::cout << "  iterations:                " << l_iterCount << std::endl;
  std::cout << "  cells:                     " << l_nCells << std::endl;
  std::cout << "  compute wall-clock:        " << l_computeSec << " s"
            << std::endl;
  if (l_iterCount > 0 && l_nCells > 0) {
    double l_perCellIter = l_computeSec / (static_cast<double>(l_nCells) *
                                           static_cast<double>(l_iterCount));
    std::cout << "  time per cell & iteration: " << l_perCellIter * 1e9 << " ns"
              << std::endl;
  }

  // free memory
  std::cout << "freeing memory" << std::endl;
  delete l_netCdf;
  delete l_stations;
  delete l_setup;
  delete l_waveProp;

  // print output dir
  std::cout << "\033[32mSimulation results written to:\033[36m " << l_simDir
            << "\033[0m" << std::endl;
  std::cout << "finished, exiting" << std::endl;
  return EXIT_SUCCESS;
}
