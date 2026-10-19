#include <stdexcept>
#include "relation_roles.h"

uint16_t RelationRoles::getOrAddRole(const std::string& role) {
	auto it = roleIndexes.find(role);
	if (it != roleIndexes.end())
		return it->second;

	if (roles.size() > UINT16_MAX)
		throw std::out_of_range("too many relation roles");
	uint16_t index = roles.size();
	roles.push_back(role);
	roleIndexes[role] = index;
	return index;
}
