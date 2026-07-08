#ifndef TSUNAMI_LAB_DISPLACEMENT_OKADADISPLACEMENT_H
#define TSUNAMI_LAB_DISPLACEMENT_OKADADISPLACEMENT_H

#include "DisplacementModel.h"

namespace tsunami_lab {
namespace displacement {

/**
 * Vertical co-seismic surface deformation (uz) of a rectangular fault in an
 * elastic half-space, after Okada (1992).
 *
 * The vertical component alone drives the solver's initial condition; the
 * horizontal components (ux/uy, via horizontalDisplacement()) exist to feed
 * the Tanioka & Satake (1996) sloping-seafloor correction, which folds them
 * back into an effective vertical displacement wherever bathymetry is steep.
 *
 * The fault is parameterised by its strike/dip/rake (degrees), slip (metres),
 * along-strike length and down-dip width (metres) and the depth of its top
 * edge (metres, > 0). The horizontal centroid sits at the local origin and the
 * query frame is (+east, +north); strike is measured clockwise from North.
 *
 * @note The caller applies this displacement only where bathymetry < 0 (ocean
 *       cells). The origin passed to verticalDisplacement() is already centred
 *       on the click location in the simulation's metre frame.
 */
class OkadaDisplacement : public DisplacementModel {
public:
  /**
   * Constructs the Okada fault model from its geometric and material
   * parameters.
   *
   * @param i_strike fault strike in degrees.
   * @param i_dip    fault dip in degrees.
   * @param i_rake   fault rake in degrees.
   * @param i_slip   dislocation magnitude in metres.
   * @param i_length along-strike length in metres.
   * @param i_width  down-dip width in metres.
   * @param i_depth  depth of the fault's top edge in metres (> 0).
   * @param i_nu     Poisson's ratio (0.25 for a Poisson solid).
   */
  OkadaDisplacement(double i_strike,
                    double i_dip,
                    double i_rake,
                    double i_slip,
                    double i_length,
                    double i_width,
                    double i_depth,
                    double i_nu = 0.25);

  /**
   * Evaluates the vertical surface displacement at a query point.
   *
   * @param i_east  east coordinate in metres relative to the fault centroid.
   * @param i_north north coordinate in metres relative to the fault centroid.
   * @return vertical displacement uz in metres.
   */
  double verticalDisplacement(double i_east, double i_north) const override;

  /**
   * Evaluates the horizontal surface displacement at a query point (Okada
   * 1985, eqs. 25/26; strike-slip and dip-slip terms only — this model
   * carries no tensile/opening component).
   *
   * @param i_east   east coordinate in metres relative to the fault centroid.
   * @param i_north  north coordinate in metres relative to the fault centroid.
   * @param o_east   east component of the horizontal displacement (m).
   * @param o_north  north component of the horizontal displacement (m).
   */
  void horizontalDisplacement(double i_east,
                              double i_north,
                              double& o_east,
                              double& o_north) const override;

  /**
   * Radius beyond which the displacement is negligible.
   *
   * @return influence radius in metres.
   */
  double influenceRadius() const override;

private:
  //! Along-strike length of the fault rectangle (metres).
  double m_length;
  //! Down-dip width of the fault rectangle (metres).
  double m_width;
  //! Depth of the fault's top edge (metres, > 0).
  double m_depth;
  //! Poisson's ratio.
  double m_nu;

  //! Sine of the strike angle.
  double m_sinStrike;
  //! Cosine of the strike angle.
  double m_cosStrike;
  //! Sine of the dip angle.
  double m_sinDip;
  //! Cosine of the dip angle.
  double m_cosDip;

  //! Strike-slip (U1) component of the dislocation.
  double m_u1;
  //! Dip-slip (U2) component of the dislocation.
  double m_u2;

  //! Depth of the fault's bottom edge (Okada's d = top + W * sin(dip)).
  double m_d;
};

} // namespace displacement
} // namespace tsunami_lab

#endif