#ifndef RELATION_ROLES_H
#define RELATION_ROLES_H

#include <cstdint>
#include <map>
#include <vector>
#include <string>

// Numbers the roles of relation members, so that they can be kept as numbers
class RelationRoles {
public:
	uint16_t getOrAddRole(const std::string& role);
	std::string getRole(uint16_t role) const { return roles.at(role); }

private:
	std::vector<std::string> roles = {""};
	std::map<std::string, uint16_t> roleIndexes = {{"", 0}};
};
#endif
