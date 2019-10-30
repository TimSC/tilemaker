LUA_CFLAGS := -I/usr/local/include/lua5.1 -I/usr/include/lua5.1
LUA_LIBS := -llua5.1
CXXFLAGS := -O3 -Wall -Wno-unknown-pragmas -Wno-sign-compare -std=c++11 -pthread $(CONFIG)
LIB := -L/usr/local/lib -lz $(LUA_LIBS) -lboost_program_options -lsqlite3 -lboost_filesystem -lboost_system -lprotobuf -lshp -lexpat -lboost_iostreams
INC := -I/usr/local/include -isystem ./include -I./src $(LUA_CFLAGS)

all: tilemaker

tilemaker: include/vector_tile.pb.o cppo5m/pbf/fileformat.pb.o cppo5m/pbf/osmformat.pb.o clipper/clipper.o src/mbtiles.o src/coordinates.o src/osm_store.o src/helpers.o src/output_object.o src/read_shp.o src/osm_lua_processing.o src/write_geometry.o src/shared_data.o src/tile_worker.o src/tile_data.o src/osm_mem_tiles.o src/shp_mem_tiles.o src/shp_disk_tiles.o src/osm_disk_tiles.o src/layer.o cppo5m/o5m.o cppo5m/varint.o cppo5m/OsmData.o cppo5m/osmxml.o cppo5m/utils.o cppo5m/pbf.o cppo5m/iso8601lib/iso8601.co src/tilemaker.o
	$(CXX) $(CXXFLAGS) -o tilemaker $^ $(INC) $(LIB)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $< $(INC)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -o $@ -c $< $(INC)

%.co: %.c
	$(CXX) $(CXXFLAGS) -o $@ -c $< $(INC)

cppo5m/pbf/%.pb.cc: cppo5m/proto/%.proto
	protoc -I=cppo5m/proto $< --cpp_out=cppo5m/pbf

%.pb.cc: %.proto
	protoc --proto_path=include --cpp_out=include $<

%.pb.o: %.pb.cc
	$(CXX) $(CXXFLAGS) -o $@ -c $< $(INC)

install:
	install -m 0755 tilemaker /usr/local/bin

clean:
	rm -f tilemaker src/*.o clipper/*.o include/*.o

.PHONY: install
