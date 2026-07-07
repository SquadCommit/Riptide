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

double OkadaDisplacement::influenceRadius() const {
  return std::sqrt(m_length * m_length + m_width * m_width) + 3.0 * m_depth;
}

} // namespace displacement
} // namespace tsunami_lab
