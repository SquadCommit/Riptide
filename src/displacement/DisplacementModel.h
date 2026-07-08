#ifndef TSUNAMI_LAB_DISPLACEMENT_DISPLACEMENTMODEL_H
#define TSUNAMI_LAB_DISPLACEMENT_DISPLACEMENTMODEL_H

namespace tsunami_lab {
namespace displacement {

/**
 * Abstract seafloor displacement of a seismic source, evaluated in a local
 * metre frame centred on the source (+east, +north).
 */
class DisplacementModel {
public:
  virtual ~DisplacementModel() = default;

  /**
   * Vertical displacement at a query point.
   *
   * @param i_east  east offset in metres from the source.
   * @param i_north north offset in metres from the source.
   * @return vertical displacement in metres (positive = uplift).
   */
  virtual double verticalDisplacement(double i_east, double i_north) const = 0;

  /**
   * Horizontal displacement at a query point. Feeds the Tanioka & Satake
   * (1996) sloping-seafloor correction (see effectiveVerticalDisplacement()).
   * Models without a physical horizontal component (e.g.
   * GaussianDisplacement) keep the default of (0, 0), which makes the
   * correction a no-op.
   *
   * @param i_east  east offset in metres from the source.
   * @param i_north north offset in metres from the source.
   * @param o_east  east component of the horizontal displacement (metres).
   * @param o_north north component of the horizontal displacement (metres).
   */
  virtual void horizontalDisplacement(double i_east,
                                      double i_north,
                                      double& o_east,
                                      double& o_north) const {
    (void)i_east;
    (void)i_north;
    o_east = 0.0;
    o_north = 0.0;
  }

  /**
   * Radius beyond which the displacement is negligible.
   *
   * @return influence radius in metres.
   */
  virtual double influenceRadius() const = 0;
};

/**
 * Tanioka & Satake (1996) sloping-seafloor correction: folds horizontal
 * seafloor motion over a sloping seafloor into a single effective vertical
 * displacement, so the solver's initial-condition interface can stay
 * unchanged (it only ever consumes one vertical-displacement number per
 * point; see setups::TsunamiEvent2d::getDisplacement()).
 *
 * Derivation: a rigid horizontal shift (i_uEast, i_uNorth) of the seafloor
 * moves the material that used to be at (x - i_uEast, y - i_uNorth) to
 * (x, y), so the bathymetry-elevation change at the fixed point (x, y) is,
 * to first order, -(i_uEast * i_elevGradEast + i_uNorth * i_elevGradNorth).
 *
 * @param i_uz            vertical Okada displacement (metres).
 * @param i_uEast         east component of the horizontal displacement (m).
 * @param i_uNorth        north component of the horizontal displacement (m).
 * @param i_elevGradEast  local bathymetry-elevation gradient in the east
 *                        direction (metres per metre).
 * @param i_elevGradNorth local bathymetry-elevation gradient in the north
 *                        direction (metres per metre).
 * @return effective vertical displacement (metres).
 */
inline double effectiveVerticalDisplacement(double i_uz,
                                            double i_uEast,
                                            double i_uNorth,
                                            double i_elevGradEast,
                                            double i_elevGradNorth) {
  return i_uz - (i_uEast * i_elevGradEast + i_uNorth * i_elevGradNorth);
}

} // namespace displacement
} // namespace tsunami_lab

#endif
