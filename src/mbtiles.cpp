#include "mbtiles.h"
#include "helpers.h"
#include <iostream>
#include <cmath>

using namespace sqlite;
using namespace std;

MBTiles::MBTiles() {}

MBTiles::~MBTiles() {
	if (db && inTransaction) db << "COMMIT;"; // commit all the changes if open
}

// ---- Write .mbtiles

void MBTiles::openForWriting(string &filename) {
	db.init(filename);

	db << "PRAGMA synchronous = OFF;";
	try {
		db << "PRAGMA application_id = 0x4d504258;";
	} catch(runtime_error &e) {
		cout << "Couldn't write SQLite application_id (not fatal): " << e.what() << endl;
	}
	try {
		db << "PRAGMA encoding = 'UTF-8';";
	} catch(runtime_error &e) {
		cout << "Couldn't set SQLite default encoding (not fatal): " << e.what() << endl;
	}
	try {
		db << "PRAGMA journal_mode=OFF;";
	} catch(runtime_error &e) {
		cout << "Couldn't turn journaling off (not fatal): " << e.what() << endl;
	}
	db << "PRAGMA page_size = 65536;";
	db << "VACUUM;"; // make sure page_size takes effect
	db << "CREATE TABLE IF NOT EXISTS metadata (name text, value text, UNIQUE (name));";
	db << "CREATE TABLE IF NOT EXISTS tiles (zoom_level integer, tile_column integer, tile_row integer, tile_data blob);";
	db << "CREATE UNIQUE INDEX IF NOT EXISTS tile_index on tiles (zoom_level, tile_column, tile_row);";
	preparedStatements.emplace_back(db << "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?,?,?,?);");

	db << "BEGIN;"; // begin a transaction
	cout << "Creating mbtiles at " << filename << endl;
	inTransaction = true;
}
	
void MBTiles::writeMetadata(string key, string value) {
	db << "REPLACE INTO metadata (name,value) VALUES (?,?);" << key << value;
}

void MBTiles::saveTile(int zoom, int x, int y, string *data) {
	int tmsY = pow(2, zoom) - 1 - y;
	preparedStatements[0].reset();
	preparedStatements[0] << zoom << x << tmsY && *data;
	preparedStatements[0].execute();
}

void MBTiles::closeForWriting() {
	preparedStatements[0].used(true);
}

// ---- Read mbtiles

