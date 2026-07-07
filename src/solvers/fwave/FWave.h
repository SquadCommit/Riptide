/**
 * @author Yannik Köllmann (yannik.koellmann AT uni-jena.de)
 * @author Jan Vogt (jan.vogt AT uni-jena.de)
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * F-wave solver for the shallow water equations.
 **/

#ifndef TSUNAMI_LAB_SOLVERS_FWAVE
#define TSUNAMI_LAB_SOLVERS_FWAVE

#include "../../constants.h"

#include <cmath>

// Hot inner-loop kernel: without the hint Clang keeps netUpdates as an
// out-of-line call inside the sweep loops (it exceeds the inline budget),
// which forces the net-update arrays through the stack.
#if defined(__clang__) || defined(__GNUC__)
#define TSUNAMI_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define TSUNAMI_ALWAYS_INLINE inline
#endif

namespace tsunami_lab {
namespace solvers {
class FWave;
}
} // namespace tsunami_lab

class tsunami_lab::solvers::FWave {
private:
  //! gravity constant
  static t_real constexpr m_g = 9.80665;
  //! square root of gravity constant
  static t_real constexpr m_gSqrt = 3.131557121;

  /**
   * Computes the wave speeds.
   *
   * @param i_hL height of the left side.
   * @param i_hR height of the right side.
   * @param i_uL particle velocity of the leftside.
   * @param i_uR particles velocity of the right side.
   * @param o_waveSpeedL will be set to the speed of the wave propagating to the
   * left.
   * @param o_waveSpeedR will be set to the speed of the wave propagating to the
   * right.
   **/
  static void waveSpeeds(t_real i_hL,
                         t_real i_hR,
                         t_real i_uL,
                         t_real i_uR,
                         t_real& o_waveSpeedL,
                         t_real& o_waveSpeedR) {
    // pre-compute square-root ops
    t_real l_hSqrtL = std::sqrt(i_hL);
    t_real l_hSqrtR = std::sqrt(i_hR);

    // compute FWave averages
    t_real l_hRoe = 0.5f * (i_hL + i_hR);
    t_real l_uRoe = l_hSqrtL * i_uL + l_hSqrtR * i_uR;
    l_uRoe /= l_hSqrtL + l_hSqrtR;

    // compute wave speeds
    t_real l_ghSqrtRoe = m_gSqrt * std::sqrt(l_hRoe);
    o_waveSpeedL = l_uRoe - l_ghSqrtRoe;
    o_waveSpeedR = l_uRoe + l_ghSqrtRoe;
  }

  /**
   * Computes the flux.
   *
   * @param i_h height of the left/right side.
   * @param i_hu momentum of the left/right side.
   * @param o_flux will be set to the flux function hu, h*u^2 + 1/2*g*h^2.
   **/
  static void flux(t_real i_h, t_real i_hu, t_real* o_flux) {
    // pre-compute u = hu / h
    t_real l_u = i_hu / i_h;

    // compute flux:  f(q) = [hu, hu*u + 0.5*g*h^2]^T
    o_flux[0] = i_hu;
    o_flux[1] = i_hu * l_u + 0.5f * m_g * i_h * i_h;
  }

  /**
   * Computes \delta x\psi_{i-1/2}
   *
   * @param i_hL height of the left side.
   * @param i_hR height of the right side.
   * @param i_bL bathymetry data of the left side
   * @param i_bR bathymetry data of the left side
   * @param o_deltaXPsi will be set to the value of \delta x\psi_{i-1/2} =
   * [0,-g(br-bl) (hr-hl)/2]^T.
   */
  static void deltaXPsi(
      t_real i_hL, t_real i_hR, t_real i_bL, t_real i_bR, t_real* o_deltaXPsi) {
    // compute delta b
    t_real l_deltaB = i_bR - i_bL;

    // compute average of h
    t_real l_avgH = 0.5f * (i_hR + i_hL);

    // compute  delta x * psi = [0, g * delta b * delta h/2]^T
    o_deltaXPsi[0] = 0;
    o_deltaXPsi[1] = -m_g * l_deltaB * l_avgH;
  }

  /**
   * Computes the wave strengths.
   *
   * @param i_hL height of the left side.
   * @param i_hR height of the right side.
   * @param i_huL momentum of the left side.
   * @param i_huR momentum of the right side.
   * @param i_bL bathymetry data of the left side
   * @param i_bR bathymetry data of the left side
   * @param i_waveSpeedL speed of the wave propagating to the left.
   * @param i_waveSpeedR speed of the wave propagating to the right.
   * @param o_strengthL will be set to the strength of the wave propagating to
   * the left.
   * @param o_strengthR will be set to the strength of the wave propagating to
   * the right.
   **/
  static void waveStrengths(t_real i_hL,
                            t_real i_hR,
                            t_real i_huL,
                            t_real i_huR,
                            t_real i_bL,
                            t_real i_bR,
                            t_real i_waveSpeedL,
                            t_real i_waveSpeedR,
                            t_real& o_strengthL,
                            t_real& o_strengthR) {
    // compute inverse of right eigenvector-matrix
    t_real l_detInv = 1 / (i_waveSpeedR - i_waveSpeedL);

    t_real l_rInv[2][2] = {{0}};
    l_rInv[0][0] = l_detInv * i_waveSpeedR;
    l_rInv[0][1] = -l_detInv;
    l_rInv[1][0] = -l_detInv * i_waveSpeedL;
    l_rInv[1][1] = l_detInv;

    // compute jump in fluxes: delta_f = f(q_r) - f(q_l)
    t_real l_fL[2] = {0};
    flux(i_hL, i_huL, l_fL);
    t_real l_fR[2] = {0};
    flux(i_hR, i_huR, l_fR);

    // compute delta x psi
    t_real l_deltaXPsi[2] = {0};
    deltaXPsi(i_hL, i_hR, i_bL, i_bR, l_deltaXPsi);

    t_real l_fluxJump[2] = {0};
    l_fluxJump[0] = l_fR[0] - l_fL[0] - l_deltaXPsi[0];
    l_fluxJump[1] = l_fR[1] - l_fL[1] - l_deltaXPsi[1];

    // compute wave strengths: alpha = R^{-1} * delta_f
    o_strengthL = l_rInv[0][0] * l_fluxJump[0];
    o_strengthL += l_rInv[0][1] * l_fluxJump[1];

    o_strengthR = l_rInv[1][0] * l_fluxJump[0];
    o_strengthR += l_rInv[1][1] * l_fluxJump[1];
  }

public:
  /**
   * Computes the net-updates.
   *
   * @param i_hL height of the left side.
   * @param i_hR height of the right side.
   * @param i_huL momentum of the left side.
   * @param i_huR momentum of the right side.
   * @param i_bL bathymetry data of the left side
   * @param i_bR bathymetry data of the left side
   * @param o_netUpdateL will be set to the net-updates for the left side; 0:
   * height, 1: momentum.
   * @param o_netUpdateR will be set to the net-updates for the right side; 0:
   * height, 1: momentum.
   *
   * @remark sides with i_hL/i_hR <= c_dryTolerance are treated as dry.
   **/
  TSUNAMI_ALWAYS_INLINE static void netUpdates(t_real i_hL,
                                               t_real i_hR,
                                               t_real i_huL,
                                               t_real i_huR,
                                               t_real i_bL,
                                               t_real i_bR,
                                               t_real o_netUpdateL[2],
                                               t_real o_netUpdateR[2]) {
    // Wet/dry handling: reflect at the wet-dry interface instead of discarding
    // the edge. The dry side is mirrored from the wet side (h, -hu, b): this
    // turns the coast into a proper reflecting wall, and i_bL = i_bR zeroes
    // the bed-slope source term across the steep coastal step. Together they
    // prevent the one-sided momentum runaway that would otherwise drive the
    // water height negative there.
    //
    // Written branch-free (ternaries compile to vector selects) so the sweep
    // loops in WavePropagation1d/2d auto-vectorize through this function.
    // Both sides dry produces garbage (0/0) in the intermediate values, but
    // the do-update masks force both net-updates to zero in that case (see
    // FWave.test.cpp's "[FWaveDryWet]" for coverage).
    bool l_dryL = i_hL <= c_dryTolerance;
    bool l_dryR = i_hR <= c_dryTolerance;
    bool l_doUpdateLeft = !l_dryL;
    bool l_doUpdateRight = !l_dryR;

    t_real l_hL = l_dryL ? i_hR : i_hL;
    t_real l_huL = l_dryL ? -i_huR : i_huL;
    t_real l_bL = l_dryL ? i_bR : i_bL;
    t_real l_hR = l_dryR ? i_hL : i_hR;
    t_real l_huR = l_dryR ? -i_huL : i_huR;
    t_real l_bR = l_dryR ? i_bL : i_bR;
    i_hL = l_hL;
    i_huL = l_huL;
    i_bL = l_bL;
    i_hR = l_hR;
    i_huR = l_huR;
    i_bR = l_bR;

    // compute velocities: u = hu / h
    t_real l_uL = i_huL / i_hL;
    t_real l_uR = i_huR / i_hR;

    // compute wave speeds (Roe eigenvalues):  lambda_1 = u_Roe - sqrt(g *
    // h_Roe); lambda_2 = u_Roe + sqrt(g * h_Roe)
    t_real l_waveSpeedL = 0;
    t_real l_waveSpeedR = 0;

    waveSpeeds(i_hL, i_hR, l_uL, l_uR, l_waveSpeedL, l_waveSpeedR);

    // compute wave strengths (eigencoefficients): [a_1, a_2]^T = R⁻¹ * delta
    // f; delta f = f(q_r) - f(q_l)
    t_real l_waveStrengthL = 0;
    t_real l_waveStrengthR = 0;

    waveStrengths(i_hL, i_hR, i_huL, i_huR, i_bL, i_bR, l_waveSpeedL,
                  l_waveSpeedR, l_waveStrengthL, l_waveStrengthR);

    // compute waves:  Z_p = a_p * r_p, with r_p = [1, lambda_p]^T
    t_real l_waveL[2] = {0};
    t_real l_waveR[2] = {0};

    l_waveL[0] = l_waveStrengthL;
    l_waveL[1] = l_waveStrengthL * l_waveSpeedL;

    l_waveR[0] = l_waveStrengthR;
    l_waveR[1] = l_waveStrengthR * l_waveSpeedR;

    // set net-updates depending on wave speeds:
    // A^- delta Q = sum of Z_p where lambda_p < 0  (left-going waves)
    // A^+ delta Q = sum of Z_p where lambda_p > 0 (right-going waves)
    //
    // Branch-free selects; the four direction predicates are written out
    // separately (not negations of each other) so NaN wave speeds keep the
    // original semantics: every comparison with NaN is false, hence no
    // contribution at all.
    bool l_w1GoesL = l_waveSpeedL < 0;
    bool l_w1GoesR = l_waveSpeedL >= 0;
    bool l_w2GoesR = l_waveSpeedR > 0;
    bool l_w2GoesL = l_waveSpeedR <= 0;

    for (unsigned short l_qt = 0; l_qt < 2; l_qt++) {
      t_real l_updL = (l_w1GoesL ? l_waveL[l_qt] : 0.0f) +
                      (l_w2GoesL ? l_waveR[l_qt] : 0.0f);
      t_real l_updR = (l_w1GoesR ? l_waveL[l_qt] : 0.0f) +
                      (l_w2GoesR ? l_waveR[l_qt] : 0.0f);

      // updates to a reflected (dry) side are dropped; this also forces the
      // both-sides-dry case (NaN intermediates) to exactly zero.
      o_netUpdateL[l_qt] = l_doUpdateLeft ? l_updL : 0.0f;
      o_netUpdateR[l_qt] = l_doUpdateRight ? l_updR : 0.0f;
    }
  }
};

#endif
