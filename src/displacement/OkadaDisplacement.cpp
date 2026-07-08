/**
 * @author Mika Brückner (mika.brueckner AT uni-jena.de)
 *
 * @section DESCRIPTION
 * Vertical co-seismic seafloor displacement of a rectangular fault in an
 * elastic half-space, after Okada (1992).
 **/
#include "OkadaDisplacement.h"

#include <algorithm>
#include <cmath>

namespace tsunami_lab {
namespace displacement {

namespace {
// Degrees to radians.
constexpr double k_deg2rad = M_PI / 180.0;
// Small guard keeping denominators / log arguments away from zero. Doubles as
// the threshold below which cos(dip) is treated as the vertical-fault limit.
constexpr double k_eps = 1e-9;
} // namespace

OkadaDisplacement::OkadaDisplacement(double i_strike,
                                     double i_dip,
                                     double i_rake,
                                     double i_slip,
                                     double i_length,
                                     double i_width,
                                     double i_depth,
                                     double i_nu)
    : m_length(i_length), m_width(i_width), m_depth(i_depth), m_nu(i_nu) {
  double l_strikeRad = i_strike * k_deg2rad;
  double l_dipRad = i_dip * k_deg2rad;
  double l_rakeRad = i_rake * k_deg2rad;

  m_sinStrike = std::sin(l_strikeRad);
  m_cosStrike = std::cos(l_strikeRad);
  m_sinDip = std::sin(l_dipRad);
  m_cosDip = std::cos(l_dipRad);

  m_u1 = i_slip * std::cos(l_rakeRad); // strike-slip component
  m_u2 = i_slip * std::sin(l_rakeRad); // dip-slip component

  // Depth of the fault's bottom edge: top edge plus the down-dip drop.
  m_d = i_depth + i_width * m_sinDip;
}

double OkadaDisplacement::verticalDisplacement(double i_east,
                                               double i_north) const {
  const double l_sinD = m_sinDip;
  const double l_cosD = m_cosDip;
  const double l_nu = m_nu;

  // Keep a value away from zero, preserving its sign, to guard divisions.
  auto l_nz = [](double i_v) {
    if (std::abs(i_v) < k_eps) {
      return i_v < 0.0 ? -k_eps : k_eps;
    }
    return i_v;
  };

  // Okada term I4 (Okada 1985), with the cos(dip) -> 0 vertical-fault limit.
  auto l_i4 = [&](double i_db, double i_eta, double i_q, double i_R) {
    if (l_cosD > k_eps) {
      return (1.0 - 2.0 * l_nu) * (1.0 / l_cosD) *
             (std::log(std::max(i_R + i_db, k_eps)) -
              l_sinD * std::log(std::max(i_R + i_eta, k_eps)));
    }
    return -(1.0 - 2.0 * l_nu) * i_q / l_nz(i_R + i_db);
  };

  // Okada term I5, with the cos(dip) -> 0 vertical-fault limit.
  auto l_i5 = [&](double i_xi, double i_eta, double i_q, double i_R,
                  double i_db) {
    if (l_cosD > k_eps) {
      if (std::abs(i_xi) < k_eps) {
        return 0.0;
      }
      double l_X = std::sqrt(i_xi * i_xi + i_q * i_q);
      double l_num = i_eta * (l_X + i_q * l_cosD) + l_X * (i_R + l_X) * l_sinD;
      double l_den = i_xi * (i_R + l_X) * l_cosD;
      return (1.0 - 2.0 * l_nu) * (2.0 / l_cosD) *
             std::atan(l_num / l_nz(l_den));
    }
    return -(1.0 - 2.0 * l_nu) * i_xi * l_sinD / l_nz(i_R + i_db);
  };

  // Strike-slip uz corner function (Okada 1992, Eq. 28).
  auto l_uzSs = [&](double i_xi, double i_eta, double i_q) {
    double l_R = std::sqrt(i_xi * i_xi + i_eta * i_eta + i_q * i_q);
    double l_db = i_eta * l_sinD - i_q * l_cosD;
    return l_db * i_q / l_nz(l_R * (l_R + i_eta)) +
           i_q * l_sinD / l_nz(l_R + i_eta) +
           l_i4(l_db, i_eta, i_q, l_R) * l_sinD;
  };

  // Dip-slip uz corner function (Okada 1992, Eq. 28).
  auto l_uzDs = [&](double i_xi, double i_eta, double i_q) {
    double l_R = std::sqrt(i_xi * i_xi + i_eta * i_eta + i_q * i_q);
    double l_db = i_eta * l_sinD - i_q * l_cosD;
    double l_u = l_db * i_q / l_nz(l_R * (l_R + i_xi)) -
                 l_i5(i_xi, i_eta, i_q, l_R, l_db) * l_sinD * l_cosD;
    if (std::abs(i_q) > k_eps) {
      l_u += l_sinD * std::atan(i_xi * i_eta / l_nz(i_q * l_R));
    }
    return l_u;
  };

  // Rotate the (+east, +north) query into the fault-local frame where +x runs
  // along strike. Strike is geographic (clockwise from North, 0 = fault runs
  // north), so the along-strike unit vector is (sin, cos) in (east, north) —
  // the same mapping as the okada85.m reference implementation.
  double l_x = i_east * m_sinStrike + i_north * m_cosStrike;
  double l_y = -i_east * m_cosStrike + i_north * m_sinStrike;

  // Okada reference coordinates relative to the fault corner.
  double l_xo = l_x + m_length / 2.0;
  double l_yo = l_y + m_cosDip * m_width / 2.0;
  double l_p = l_yo * m_cosDip + m_d * m_sinDip;
  double l_q = l_yo * m_sinDip - m_d * m_cosDip;

  // Chinnery's notation: f(x2,y2) - f(x2,y1) - f(x1,y2) + f(x1,y1) with
  // xi in {x, x-L} and eta in {p, p-W}. q is constant across the four corners.
  double l_x1 = l_xo - m_length;
  double l_y1 = l_p - m_width;

  double l_ss = l_uzSs(l_xo, l_p, l_q) - l_uzSs(l_xo, l_y1, l_q) -
                l_uzSs(l_x1, l_p, l_q) + l_uzSs(l_x1, l_y1, l_q);
  double l_ds = l_uzDs(l_xo, l_p, l_q) - l_uzDs(l_xo, l_y1, l_q) -
                l_uzDs(l_x1, l_p, l_q) + l_uzDs(l_x1, l_y1, l_q);

  return -m_u1 / (2.0 * M_PI) * l_ss - m_u2 / (2.0 * M_PI) * l_ds;
}

void OkadaDisplacement::horizontalDisplacement(double i_east,
                                               double i_north,
                                               double& o_east,
                                               double& o_north) const {
  const double l_sinD = m_sinDip;
  const double l_cosD = m_cosDip;
  const double l_nu = m_nu;

  // Duplicated from verticalDisplacement() rather than shared, so that
  // method (already cross-checked against okada85.m) is untouched by this
  // addition.
  auto l_nz = [](double i_v) {
    if (std::abs(i_v) < k_eps) {
      return i_v < 0.0 ? -k_eps : k_eps;
    }
    return i_v;
  };

  auto l_i4 = [&](double i_db, double i_eta, double i_q, double i_R) {
    if (l_cosD > k_eps) {
      return (1.0 - 2.0 * l_nu) * (1.0 / l_cosD) *
             (std::log(std::max(i_R + i_db, k_eps)) -
              l_sinD * std::log(std::max(i_R + i_eta, k_eps)));
    }
    return -(1.0 - 2.0 * l_nu) * i_q / l_nz(i_R + i_db);
  };

  auto l_i5 = [&](double i_xi, double i_eta, double i_q, double i_R,
                  double i_db) {
    if (l_cosD > k_eps) {
      if (std::abs(i_xi) < k_eps) {
        return 0.0;
      }
      double l_X = std::sqrt(i_xi * i_xi + i_q * i_q);
      double l_num = i_eta * (l_X + i_q * l_cosD) + l_X * (i_R + l_X) * l_sinD;
      double l_den = i_xi * (i_R + l_X) * l_cosD;
      return (1.0 - 2.0 * l_nu) * (2.0 / l_cosD) *
             std::atan(l_num / l_nz(l_den));
    }
    return -(1.0 - 2.0 * l_nu) * i_xi * l_sinD / l_nz(i_R + i_db);
  };

  // Okada term I3 (Okada 1985, eq. 28); needed by I2.
  auto l_i3 = [&](double i_eta, double i_q, double i_R) {
    double l_db = i_eta * l_sinD - i_q * l_cosD;
    double l_yb = i_eta * l_cosD + i_q * l_sinD;
    if (l_cosD > k_eps) {
      return (1.0 - 2.0 * l_nu) * (l_yb / l_nz(l_cosD * (i_R + l_db)) -
                                   std::log(std::max(i_R + i_eta, k_eps))) +
             (l_sinD / l_cosD) * l_i4(l_db, i_eta, i_q, i_R);
    }
    double l_Rdb = l_nz(i_R + l_db);
    return (1.0 - 2.0 * l_nu) / 2.0 *
           (i_eta / l_Rdb + l_yb * i_q / (l_Rdb * l_Rdb) -
            std::log(std::max(i_R + i_eta, k_eps)));
  };

  // Okada term I2; needed by uySs.
  auto l_i2 = [&](double i_eta, double i_q, double i_R) {
    return (1.0 - 2.0 * l_nu) * (-std::log(std::max(i_R + i_eta, k_eps))) -
           l_i3(i_eta, i_q, i_R);
  };

  // Okada term I1; needed by uxSs and uyDs.
  auto l_i1 = [&](double i_xi, double i_eta, double i_q, double i_R) {
    double l_db = i_eta * l_sinD - i_q * l_cosD;
    if (l_cosD > k_eps) {
      return (1.0 - 2.0 * l_nu) * (-i_xi / l_nz(l_cosD * (i_R + l_db))) -
             (l_sinD / l_cosD) * l_i5(i_xi, i_eta, i_q, i_R, l_db);
    }
    double l_Rdb = l_nz(i_R + l_db);
    return -(1.0 - 2.0 * l_nu) / 2.0 * i_xi * i_q / (l_Rdb * l_Rdb);
  };

  // Strike-slip corner functions for ux, uy (Okada 1985, eq. 25).
  auto l_uxSs = [&](double i_xi, double i_eta, double i_q) {
    double l_R = std::sqrt(i_xi * i_xi + i_eta * i_eta + i_q * i_q);
    double l_u = i_xi * i_q / l_nz(l_R * (l_R + i_eta)) +
                 l_i1(i_xi, i_eta, i_q, l_R) * l_sinD;
    if (std::abs(i_q) > k_eps) {
      l_u += std::atan(i_xi * i_eta / l_nz(i_q * l_R));
    }
    return l_u;
  };

  auto l_uySs = [&](double i_xi, double i_eta, double i_q) {
    double l_R = std::sqrt(i_xi * i_xi + i_eta * i_eta + i_q * i_q);
    return (i_eta * l_cosD + i_q * l_sinD) * i_q / l_nz(l_R * (l_R + i_eta)) +
           i_q * l_cosD / l_nz(l_R + i_eta) + l_i2(i_eta, i_q, l_R) * l_sinD;
  };

  // Dip-slip corner functions for ux, uy (Okada 1985, eq. 26).
  auto l_uxDs = [&](double i_xi, double i_eta, double i_q) {
    double l_R = std::sqrt(i_xi * i_xi + i_eta * i_eta + i_q * i_q);
    return i_q / l_nz(l_R) - l_i3(i_eta, i_q, l_R) * l_sinD * l_cosD;
  };

  auto l_uyDs = [&](double i_xi, double i_eta, double i_q) {
    double l_R = std::sqrt(i_xi * i_xi + i_eta * i_eta + i_q * i_q);
    double l_u =
        (i_eta * l_cosD + i_q * l_sinD) * i_q / l_nz(l_R * (l_R + i_xi)) -
        l_i1(i_xi, i_eta, i_q, l_R) * l_sinD * l_cosD;
    if (std::abs(i_q) > k_eps) {
      l_u += l_cosD * std::atan(i_xi * i_eta / l_nz(i_q * l_R));
    }
    return l_u;
  };

  // Same fault-local geometry as verticalDisplacement().
  double l_x = i_east * m_sinStrike + i_north * m_cosStrike;
  double l_y = -i_east * m_cosStrike + i_north * m_sinStrike;

  double l_xo = l_x + m_length / 2.0;
  double l_yo = l_y + m_cosDip * m_width / 2.0;
  double l_p = l_yo * m_cosDip + m_d * m_sinDip;
  double l_q = l_yo * m_sinDip - m_d * m_cosDip;

  double l_x1 = l_xo - m_length;
  double l_y1 = l_p - m_width;

  double l_uxSsSum = l_uxSs(l_xo, l_p, l_q) - l_uxSs(l_xo, l_y1, l_q) -
                     l_uxSs(l_x1, l_p, l_q) + l_uxSs(l_x1, l_y1, l_q);
  double l_uxDsSum = l_uxDs(l_xo, l_p, l_q) - l_uxDs(l_xo, l_y1, l_q) -
                     l_uxDs(l_x1, l_p, l_q) + l_uxDs(l_x1, l_y1, l_q);
  double l_uySsSum = l_uySs(l_xo, l_p, l_q) - l_uySs(l_xo, l_y1, l_q) -
                     l_uySs(l_x1, l_p, l_q) + l_uySs(l_x1, l_y1, l_q);
  double l_uyDsSum = l_uyDs(l_xo, l_p, l_q) - l_uyDs(l_xo, l_y1, l_q) -
                     l_uyDs(l_x1, l_p, l_q) + l_uyDs(l_x1, l_y1, l_q);

  double l_ux =
      -m_u1 / (2.0 * M_PI) * l_uxSsSum - m_u2 / (2.0 * M_PI) * l_uxDsSum;
  double l_uy =
      -m_u1 / (2.0 * M_PI) * l_uySsSum - m_u2 / (2.0 * M_PI) * l_uyDsSum;

  // Rotate from the fault-local frame (+x along strike) back to geographic
  // (east, north) — okada85.m: ue = sinStrike*ux - cosStrike*uy;
  //                             un = cosStrike*ux + sinStrike*uy.
  o_east = m_sinStrike * l_ux - m_cosStrike * l_uy;
  o_north = m_cosStrike * l_ux + m_sinStrike * l_uy;
}

double OkadaDisplacement::influenceRadius() const {
  return std::sqrt(m_length * m_length + m_width * m_width) + 3.0 * m_depth;
}

} // namespace displacement
} // namespace tsunami_lab
