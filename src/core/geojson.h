// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Minimal GeoJSON reader for the map-type Scope. No SDL, no GDAL, and no
// external dependencies: accepts FeatureCollection with Polygon/MultiPolygon
// (and Point/MultiPoint, stored for drawing markers).
#pragma once
#include <map>
#include <string>
#include <vector>

struct GeoPoint {
    double x = 0, y = 0;
};

struct GeoRing {
    std::vector<GeoPoint> pts;
};

struct GeoFeature {
    std::string id;                 // set by geoAssignFields
    std::string label;              // same
    std::map<std::string, std::string> props;
    std::vector<std::vector<GeoRing>> polygons; // polygon -> rings (0 = exterior)
    std::vector<std::vector<GeoPoint>> lines;    // LineString/MultiLineString
    std::vector<GeoPoint> points;   // Point/MultiPoint
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    double cx = 0, cy = 0;
    bool hasGeom = false;
};

struct GeoMap {
    std::vector<GeoFeature> features;
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    bool mercator = false; // true if the coordinates were projected to Mercator
    std::string error;
    bool ok() const { return error.empty() && !features.empty(); }
};

// Reads and validates a file. On error, returns false and fills err.
bool geoJSONLoad(const std::string& path, GeoMap& out, std::string& err);

// Same contract, from text (useful in tests).
bool geoJSONParse(const std::string& text, GeoMap& out, std::string& err);

// Sets id/label from the properties. An empty idField uses "id" or the
// index; an empty labelField tries "nome"/"name"/"label".
void geoAssignFields(GeoMap& map, const std::string& idField, const std::string& labelField);

// Projects in place. mode: "auto", "mercator", or "linear".
// "auto" uses Mercator when the coordinates look like WGS84 lon/lat.
bool geoProject(GeoMap& map, const std::string& mode, std::string& err);

// Projects a single point (lon/lat or already projected) to the same map plane.
void geoProjectPoint(double& x, double& y, bool mercator);

// Recomputes bbox/centroids after a transformation.
void geoComputeBounds(GeoMap& map);

// Point inside a feature (coordinates already projected), using the even-odd rule.
bool geoFeatureContains(const GeoFeature& f, double x, double y);

// Shortest distance from the point to a LineString/MultiLineString (∞ if none).
double geoFeatureLineDistance(const GeoFeature& f, double x, double y);

// Index of the feature under the point (projected coordinates) or -1. tol > 0 also
// accepts lines within tol distance units (useful for highway hit-testing).
int geoFeatureAt(const GeoMap& map, double x, double y, double tol = 0.0);
