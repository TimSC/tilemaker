/*! \file */ 
#ifndef _MBTILES_H
#define _MBTILES_H

#include <string>
#include <vector>
#include "external/sqlite_modern_cpp.h"

/** \brief Write to MBTiles (sqlite) database
*
* (note that sqlite_modern_cpp.h is very slightly changed from the original, for blob support and an .init method)
*/
class MBTiles { 
	sqlite::database db;
	std::vector<sqlite::database_binder> preparedStatements;
	bool inTransaction = false;

public:
	MBTiles();
	virtual ~MBTiles();
	void openForWriting(std::string &filename);
	void writeMetadata(std::string key, std::string value);
	void saveTile(int zoom, int x, int y, std::string *data);
	void closeForWriting();

};

#endif //_MBTILES_H

