// Reads an area of a pycrocosm map database. Like pgmap, it reads two layers
// of tables: the static layer (the imported planet, pgmap's dbtableprefix) and
// the modify layer over it (the edits since, dbtablemodifyprefix). An object
// is the modify layer's if it has taken the object over (its ID is in the
// layer's nodeids, wayids or relationids, whether edited or deleted), and
// otherwise the static layer's. This is what pgmap's <prefix>visible* views
// show, but here either prefix can be chosen.
//
// Ways and relations are found by their bounding boxes, which the map must
// store. Their shapes come from the shape cache (way_shapes and
// relation_shapes) when it is on and has them; otherwise they are built from
// their nodes, which are fetched by ID where they lie outside the area.
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <libpq-fe.h>
#include "rapidjson/document.h"
#include "osm_area.h"

using namespace std;

extern bool verbose;

namespace {

// The map schema versions whose views this reader has been checked against.
// The views have named their columns since 15; 16 added atomic edit IDs.
const set<int> SUPPORTED_SCHEMA_VERSIONS = {15, 16};

using Result = unique_ptr<PGresult, decltype(&PQclear)>;

class Database {
public:
	explicit Database(const string& conninfo): conn(PQconnectdb(conninfo.c_str()), &PQfinish) {
		if (PQstatus(conn.get()) != CONNECTION_OK)
			throw runtime_error(string("Couldn't connect to the map database: ") + PQerrorMessage(conn.get()));
	}

	string quoteName(const string& name) {
		char* quoted = PQescapeIdentifier(conn.get(), name.c_str(), name.size());
		if (quoted == nullptr) throw runtime_error(PQerrorMessage(conn.get()));
		string out(quoted);
		PQfreemem(quoted);
		return out;
	}

	Result exec(const string& sql, const vector<string>& params = {}) {
		auto start = chrono::steady_clock::now();
		vector<const char*> values;
		for (const string& p : params) values.push_back(p.c_str());
		Result r(PQexecParams(conn.get(), sql.c_str(), values.size(), nullptr, values.data(), nullptr, nullptr, 0), &PQclear);
		ExecStatusType status = PQresultStatus(r.get());
		if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK)
			throw runtime_error(string("Map database query failed: ") + PQresultErrorMessage(r.get()));
		if (verbose) {
			auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
			cout << ms << "ms, " << PQntuples(r.get()) << " rows: " << sql.substr(0, 120) << endl;
		}
		return r;
	}

private:
	unique_ptr<PGconn, decltype(&PQfinish)> conn;
};

int64_t toInt64(const char* s) { return stoll(s); }

// A PostgreSQL array literal of IDs
string idArray(const vector<int64_t>& ids) {
	string out = "{";
	for (size_t i = 0; i < ids.size(); i++) {
		if (i) out += ",";
		out += to_string(ids[i]);
	}
	return out + "}";
}

void parseJson(const char* text, rapidjson::Document& doc) {
	doc.Parse(text);
	if (doc.HasParseError()) throw runtime_error(string("Bad JSON in the map database: ") + text);
}

OsmAreaTags parseTags(const char* text) {
	OsmAreaTags tags;
	rapidjson::Document doc;
	parseJson(text, doc);
	if (!doc.IsObject()) return tags;
	for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it)
		if (it->value.IsString())
			tags.emplace_back(it->name.GetString(), it->value.GetString());
	return tags;
}

// A way's members: an array of node IDs
vector<int64_t> parseWayMembers(const char* text) {
	vector<int64_t> refs;
	rapidjson::Document doc;
	parseJson(text, doc);
	if (!doc.IsArray()) return refs;
	for (const auto& v : doc.GetArray())
		if (v.IsInt64()) refs.push_back(v.GetInt64());
	return refs;
}

// A relation's members, [["way", id], ...], and their roles, ["outer", ...]
vector<OsmAreaMember> parseRelationMembers(const char* membersText, const char* rolesText) {
	vector<OsmAreaMember> members;
	rapidjson::Document doc, roles;
	parseJson(membersText, doc);
	parseJson(rolesText, roles);
	if (!doc.IsArray()) return members;
	for (rapidjson::SizeType i = 0; i < doc.Size(); i++) {
		const auto& m = doc[i];
		if (!m.IsArray() || m.Size() < 2 || !m[0].IsString() || !m[1].IsInt64()) continue;
		string type = m[0].GetString();
		OsmAreaMember member;
		member.type = type == "node" ? OsmAreaMember::Type::Node :
		              type == "way" ? OsmAreaMember::Type::Way : OsmAreaMember::Type::Relation;
		member.id = m[1].GetInt64();
		if (roles.IsArray() && i < roles.Size() && roles[i].IsString())
			member.role = roles[i].GetString();
		members.push_back(std::move(member));
	}
	return members;
}

// The objects of each kind the map shows, from one or two layers
class Layers {
public:
	Layers(Database& db, const string& staticPrefix, const string& modifyPrefix):
		db(db), staticPrefix(staticPrefix), modifyPrefix(modifyPrefix) {}

	// Refuses tables this reader doesn't understand. Returns whether the shape
	// cache is on.
	bool check() {
		vector<string> prefixes = {modifyPrefix};
		if (!staticPrefix.empty()) prefixes.push_back(staticPrefix);
		for (const string& prefix : prefixes) {
			string meta = db.quoteName(prefix + "meta");
			Result r = db.exec("SELECT to_regclass($1) IS NOT NULL", {meta});
			if (string(PQgetvalue(r.get(), 0, 0)) != "t")
				throw runtime_error("No map tables with the prefix " + prefix);
			r = db.exec("SELECT value FROM " + meta + " WHERE key = 'schema_version'");
			int version = PQntuples(r.get()) ? stoi(PQgetvalue(r.get(), 0, 0)) : 0;
			if (!SUPPORTED_SCHEMA_VERSIONS.count(version))
				throw runtime_error("Map schema version " + to_string(version) + " (" + prefix + ") is not supported");
		}

		for (const string& table : {own("way"), own("relation")}) {
			Result r = db.exec("SELECT count(*) FROM pg_attribute WHERE attrelid = to_regclass($1) AND attname = 'bbox' AND NOT attisdropped", {table});
			if (string(PQgetvalue(r.get(), 0, 0)) == "0")
				throw runtime_error("The map does not store way and relation bounding boxes, which reading from it needs");
		}

		bool shapes = true;
		for (const char* kind : {"way", "relation"}) {
			Result r = db.exec("SELECT to_regclass($1) IS NOT NULL AND to_regclass($2) IS NOT NULL",
				{ownShapes(kind), parentShapes(kind)});
			shapes = shapes && string(PQgetvalue(r.get(), 0, 0)) == "t";
		}
		return shapes;
	}

	// SELECT columns of the objects of a kind ("node", "way" or "relation")
	// that match where, which may use the parameters $1 and so on
	string objects(const string& kind, const string& columns, const string& where) {
		return select(columns, own(kind), parent(kind), kind, where);
	}

	// SELECT columns of the cached shapes of a kind ("way" or "relation")
	string shapes(const string& kind, const string& columns, const string& where) {
		return select(columns, ownShapes(kind), parentShapes(kind), kind, where);
	}

private:
	// The modify layer's own objects, and what it shows of the static layer's.
	// With no static prefix, the modify layer's views show everything.
	string own(const string& kind) {
		return db.quoteName(modifyPrefix + (staticPrefix.empty() ? "visible" : "live") + kind + "s");
	}
	string parent(const string& kind) { return db.quoteName(staticPrefix + "visible" + kind + "s"); }
	string ownShapes(const string& kind) {
		return db.quoteName(modifyPrefix + (staticPrefix.empty() ? "visible_" : "") + kind + "_shapes");
	}
	string parentShapes(const string& kind) {
		return db.quoteName((staticPrefix.empty() ? modifyPrefix : staticPrefix) + "visible_" + kind + "_shapes");
	}

	string select(const string& columns, const string& ownTable, const string& parentTable,
	              const string& kind, const string& where) {
		if (staticPrefix.empty())
			return "SELECT " + columns + " FROM " + ownTable + " WHERE " + where;
		string ids = db.quoteName(modifyPrefix + kind + "ids");
		return "SELECT " + columns + " FROM " + parentTable + " p WHERE (" + where + ") AND NOT EXISTS "
			"(SELECT 1 FROM " + ids + " x WHERE x.id = p.id) "
			"UNION ALL SELECT " + columns + " FROM " + ownTable + " WHERE " + where;
	}

	Database& db;
	string staticPrefix, modifyPrefix;
};

class AreaReader {
public:
	AreaReader(Database& db, Layers& layers): db(db), layers(layers) {}

	void read(const vector<string>& box, bool useShapes, OsmArea& area) {
		const string envelope = "ST_MakeEnvelope($1::float8, $2::float8, $3::float8, $4::float8, 4326)";

		// Relations reaching into the area
		readRelations("bbox && " + envelope, box, area);
		if (useShapes) readRelationShapes(area);

		// Ways reaching into the area, and the member ways of multipolygons
		// the shape cache can't give, which are needed to build them
		readWays("bbox && " + envelope, box, area);
		vector<int64_t> missingWays;
		for (const OsmAreaRelation& relation : area.relations) {
			if (!relation.shapeWkt.empty() || !isMultipolygon(relation)) continue;
			for (const OsmAreaMember& m : relation.members)
				if (m.type == OsmAreaMember::Type::Way && !wayIds.count(m.id)) {
					missingWays.push_back(m.id);
					wayIds.insert(m.id);
				}
		}
		if (!missingWays.empty())
			readWays("id = ANY($1::bigint[])", {idArray(missingWays)}, area);
		if (useShapes) readWayShapes(area);

		// Nodes in the area, then any outside it that ways without a cached
		// shape need, and relations' node members (for labels)
		readNodes("geom && " + envelope, box, area);
		vector<int64_t> missingNodes;
		auto need = [&](int64_t id) {
			if (nodeIds.insert(id).second) missingNodes.push_back(id);
		};
		for (const OsmAreaWay& way : area.ways)
			if (way.shapeWkt.empty())
				for (int64_t ref : way.refs) need(ref);
		for (const OsmAreaRelation& relation : area.relations)
			for (const OsmAreaMember& m : relation.members)
				if (m.type == OsmAreaMember::Type::Node) need(m.id);
		if (!missingNodes.empty())
			readNodes("id = ANY($1::bigint[])", {idArray(missingNodes)}, area);
	}

private:
	static bool isMultipolygon(const OsmAreaRelation& relation) {
		for (const auto& tag : relation.tags)
			if (tag.first == "type") return tag.second == "multipolygon";
		return false;
	}

	void readNodes(const string& where, const vector<string>& params, OsmArea& area) {
		Result r = db.exec(layers.objects("node", "id, ST_Y(geom), ST_X(geom), tags::text", where), params);
		for (int i = 0; i < PQntuples(r.get()); i++) {
			int64_t id = toInt64(PQgetvalue(r.get(), i, 0));
			nodeIds.insert(id);
			area.nodes.push_back({id, stod(PQgetvalue(r.get(), i, 1)), stod(PQgetvalue(r.get(), i, 2)),
				parseTags(PQgetvalue(r.get(), i, 3))});
		}
	}

	void readWays(const string& where, const vector<string>& params, OsmArea& area) {
		Result r = db.exec(layers.objects("way", "id, tags::text, members::text", where), params);
		for (int i = 0; i < PQntuples(r.get()); i++) {
			int64_t id = toInt64(PQgetvalue(r.get(), i, 0));
			wayIds.insert(id);
			area.ways.push_back({id, parseWayMembers(PQgetvalue(r.get(), i, 2)), parseTags(PQgetvalue(r.get(), i, 1)), {}});
		}
	}

	void readRelations(const string& where, const vector<string>& params, OsmArea& area) {
		Result r = db.exec(layers.objects("relation", "id, tags::text, members::text, memberroles::text", where), params);
		for (int i = 0; i < PQntuples(r.get()); i++) {
			area.relations.push_back({toInt64(PQgetvalue(r.get(), i, 0)),
				parseRelationMembers(PQgetvalue(r.get(), i, 2), PQgetvalue(r.get(), i, 3)),
				parseTags(PQgetvalue(r.get(), i, 1)), {}});
		}
	}

	void readWayShapes(OsmArea& area) {
		vector<int64_t> ids;
		for (const OsmAreaWay& way : area.ways) ids.push_back(way.id);
		if (ids.empty()) return;
		Result r = db.exec(layers.shapes("way", "id, ST_AsText(shape)",
			"id = ANY($1::bigint[]) AND shape IS NOT NULL"), {idArray(ids)});
		unordered_map<int64_t, string> shapes;
		for (int i = 0; i < PQntuples(r.get()); i++)
			shapes[toInt64(PQgetvalue(r.get(), i, 0))] = PQgetvalue(r.get(), i, 1);
		for (OsmAreaWay& way : area.ways) {
			auto it = shapes.find(way.id);
			if (it != shapes.end()) way.shapeWkt = std::move(it->second);
		}
	}

	// An area's shape is a polygon or multipolygon; anything else's is a
	// collection of its members' points and lines, of which the lines are kept
	void readRelationShapes(OsmArea& area) {
		vector<int64_t> ids;
		for (const OsmAreaRelation& relation : area.relations) ids.push_back(relation.id);
		if (ids.empty()) return;
		Result r = db.exec(layers.shapes("relation", "id, CASE WHEN GeometryType(shape) IN ('POLYGON', 'MULTIPOLYGON') "
			"THEN ST_AsText(ST_Multi(shape)) ELSE ST_AsText(ST_Multi(ST_CollectionExtract(shape, 2))) END",
			"id = ANY($1::bigint[]) AND shape IS NOT NULL"), {idArray(ids)});
		unordered_map<int64_t, string> shapes;
		for (int i = 0; i < PQntuples(r.get()); i++)
			shapes[toInt64(PQgetvalue(r.get(), i, 0))] = PQgetvalue(r.get(), i, 1);
		for (OsmAreaRelation& relation : area.relations) {
			auto it = shapes.find(relation.id);
			if (it != shapes.end()) relation.shapeWkt = std::move(it->second);
		}
	}

	Database& db;
	Layers& layers;
	unordered_set<int64_t> nodeIds, wayIds;
};

}

void ReadOsmDatabase(const string& conninfo, const string& staticPrefix, const string& modifyPrefix,
	double minLon, double minLat, double maxLon, double maxLat, OsmArea& area) {
	Database db(conninfo);
	// One snapshot for every query, so the parts agree with each other
	db.exec("BEGIN READ ONLY ISOLATION LEVEL REPEATABLE READ");

	Layers layers(db, staticPrefix, modifyPrefix);
	bool useShapes = layers.check();
	AreaReader reader(db, layers);

	ostringstream box[4];
	double values[4] = {minLon, minLat, maxLon, maxLat};
	vector<string> params;
	for (int i = 0; i < 4; i++) {
		box[i].precision(17);
		box[i] << values[i];
		params.push_back(box[i].str());
	}
	reader.read(params, useShapes, area);

	db.exec("COMMIT");

	if (!area.hasBounds) {
		area.hasBounds = true;
		area.minLon = minLon;
		area.minLat = minLat;
		area.maxLon = maxLon;
		area.maxLat = maxLat;
	}
}
