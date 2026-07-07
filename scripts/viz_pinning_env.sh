#!/usr/bin/env bash
##
# Detects this machine's CPU topology and derives the environment needed to
# run tsunami_lab_viz with the GUI thread pinned to one fixed core and
# OpenMP given one worker thread per remaining performance core (see
# sphinx/source/chapters/12_individual_week3.rst).
#
# - Linux, hybrid CPUs (Intel P-core/E-core): the GUI gets one E-core,
#   OpenMP gets all P-cores. Hard pinning via TSUNAMI_VIZ_CORE/OMP_PLACES.
# - Linux, homogeneous CPUs (with or without SMT): one representative
#   logical CPU per physical core is chosen; the GUI gets the first, OpenMP
#   gets one thread per remaining physical core (SMT siblings left idle).
#   Hard pinning via TSUNAMI_VIZ_CORE/OMP_PLACES.
# - macOS: there is no user-space hard CPU-affinity API (no
#   pthread_setaffinity_np, no OMP_PLACES support in libomp), so
#   TSUNAMI_VIZ_CORE/OMP_PLACES/OMP_PROC_BIND are not applicable. Only
#   OMP_NUM_THREADS is tuned, via `sysctl` core counts (performance cores on
#   Apple Silicon, physical-core-count-minus-one otherwise) as a soft
#   heuristic — the OS scheduler still decides which core runs what.
#
# Usage:
#   eval "$(scripts/viz_pinning_env.sh)"        # then run the binary yourself
#   scripts/viz_pinning_env.sh CMD [ARGS...]    # detect + exec CMD with env set
#
# All diagnostics go to stderr so `eval "$(...)"` only ever sees the
# `export ...` lines on stdout.
##
set -euo pipefail

# Expands a cpulist like "0-2,5,8-9" (as found in sysfs/lscpu) into one id
# per line.
expand_cpulist() {
  local spec="$1" part lo hi i
  IFS=',' read -ra parts <<<"$spec"
  for part in "${parts[@]}"; do
    if [[ "$part" == *-* ]]; then
      lo=${part%-*}; hi=${part#*-}
      for ((i = lo; i <= hi; i++)); do echo "$i"; done
    else
      echo "$part"
    fi
  done
}

declare -a viz_cores=()
declare -a omp_cores=()
omp_threads=""

case "$(uname -s)" in
Darwin)
  echo "# macOS: no hard CPU-affinity API for user threads -- only OMP_NUM_THREADS is tuned" >&2
  perf_cores="$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || true)"
  if [[ -n "$perf_cores" && "$perf_cores" -gt 0 ]]; then
    eff_cores="$(sysctl -n hw.perflevel1.physicalcpu 2>/dev/null || echo '?')"
    echo "# Apple Silicon: $perf_cores performance core(s), $eff_cores efficiency core(s)" >&2
    omp_threads="$perf_cores"
  else
    total_cores="$(sysctl -n hw.physicalcpu 2>/dev/null || echo 1)"
    if [[ "$total_cores" -gt 1 ]]; then
      omp_threads=$((total_cores - 1))
      echo "# $total_cores physical core(s) total; reserving 1 for the GUI by count (not a hard pin)" >&2
    else
      omp_threads="$total_cores"
      echo "# only 1 physical core found -- nothing to reserve" >&2
    fi
  fi
  ;;

Linux)
  if [[ -r /sys/devices/cpu_core/cpus && -r /sys/devices/cpu_atom/cpus ]]; then
    echo "# hybrid CPU detected (cpu_core/cpu_atom): GUI -> E-core, OpenMP -> P-cores" >&2
    mapfile -t p_cores < <(expand_cpulist "$(cat /sys/devices/cpu_core/cpus)")
    mapfile -t e_cores < <(expand_cpulist "$(cat /sys/devices/cpu_atom/cpus)")
    if [[ ${#e_cores[@]} -eq 0 || ${#p_cores[@]} -eq 0 ]]; then
      echo "# cpu_core/cpu_atom present but empty -- falling back to homogeneous detection" >&2
    else
      viz_cores=("${e_cores[0]}")
      omp_cores=("${p_cores[@]}")
    fi
  fi

  if [[ ${#omp_cores[@]} -eq 0 ]]; then
    if ! command -v lscpu >/dev/null 2>&1; then
      echo "# lscpu not found -- cannot detect physical-core topology on this system" >&2
    else
      echo "# homogeneous CPU: grouping logical CPUs by physical core (lscpu -p)" >&2
      declare -A core_rep=()
      while IFS=, read -r cpu core online; do
        [[ "$cpu" == \#* ]] && continue
        [[ -n "${online:-Y}" && "${online:-Y}" != "Y" ]] && continue
        core_rep["$core"]="${core_rep[$core]:-$cpu}"
      done < <(lscpu -p=CPU,CORE,ONLINE)

      if [[ ${#core_rep[@]} -lt 2 ]]; then
        echo "# only ${#core_rep[@]} physical core(s) found -- no core to spare for pinning" >&2
      else
        mapfile -t reps < <(printf '%s\n' "${core_rep[@]}" | sort -n)
        viz_cores=("${reps[0]}")
        omp_cores=("${reps[@]:1}")
      fi
    fi
  fi

  if [[ ${#omp_cores[@]} -gt 0 ]]; then
    omp_threads="${#omp_cores[@]}"
    echo "# GUI core: ${viz_cores[0]}   OpenMP cores ($omp_threads): ${omp_cores[*]}" >&2
  fi
  ;;

*)
  echo "# unsupported platform for CPU-topology auto-detection: $(uname -s)" >&2
  ;;
esac

if [[ -n "$omp_threads" ]]; then
  export OMP_NUM_THREADS="$omp_threads"
fi
if [[ ${#omp_cores[@]} -gt 0 ]]; then
  places=$(printf '{%s},' "${omp_cores[@]}")
  export TSUNAMI_VIZ_CORE="${viz_cores[0]}"
  export OMP_PROC_BIND="close"
  export OMP_PLACES="${places%,}"
fi
# Unset OMP_SCHEDULE falls back to an unchunked dynamic schedule (~37x
# slower here than static, see OmpDefaults.h); only default it, don't
# override an explicit choice.
export OMP_SCHEDULE="${OMP_SCHEDULE:-static}"

if [[ $# -gt 0 ]]; then
  exec "$@"
fi

[[ -n "${TSUNAMI_VIZ_CORE:-}" ]] && echo "export TSUNAMI_VIZ_CORE=$TSUNAMI_VIZ_CORE"
[[ -n "$omp_threads" ]] && echo "export OMP_NUM_THREADS=$omp_threads"
[[ -n "${OMP_PROC_BIND:-}" ]] && echo "export OMP_PROC_BIND=$OMP_PROC_BIND"
[[ -n "${OMP_PLACES:-}" ]] && echo "export OMP_PLACES=\"$OMP_PLACES\""
