#ifndef TSUNAMI_LAB_WEB_SLAB2WEB_H
#define TSUNAMI_LAB_WEB_SLAB2WEB_H

#include "../io/Slab2Reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace tsunami_lab {
namespace web {

/**
 * Parses the pre-converted Slab2 bundle (tools/make_web_data.py) into a
 * regular io::Slab2Reader, so query()/OkadaFactory/the overlays behave
 * exactly like the native NetCDF-backed reader.
 *
 * Binary format "TLS2" (little endian, served gzip-compressed and inflated
 * by the frontend via DecompressionStream):
 *   char[4]  magic "TLS2"
 *   int32    region count
 *   per region:
 *     uint8   nameLen, char name[nameLen]        UTF-8 display name
 *     float64 lonMin, latMin, dLon, dLat          axis origin + signed spacing
 *     int32   nx, ny
 *     int16   dep[nx*ny]   slab depth, unit 100 m positive-down, -32768 = no
 * slab int16   str[nx*ny]   strike, unit 0.1 deg,                 -32768 = no
 * slab int16   dip[nx*ny]   dip,    unit 0.1 deg,                 -32768 = no
 * slab
 *
 * @return the reader, or nullptr on malformed input.
 **/
std::unique_ptr<io::Slab2Reader> parseSlab2(const uint8_t* i_data,
                                            size_t i_size);

} // namespace web
} // namespace tsunami_lab

#endif
