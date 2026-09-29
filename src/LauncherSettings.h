#pragma once
#include <map>
#include <set>
#include <sstream>
#include <string>
namespace argent::launcher {
// Replace launcher-owned single-value settings only. Calibration/profile rows
// and comments are preserved verbatim, including profiles unknown to the UI.
inline std::string updateSettings(std::istream& input,std::map<std::string,std::string> values){
 std::ostringstream output;std::string line;std::set<std::string> written;
 while(std::getline(input,line)){
  std::istringstream row(line);std::string key;row>>key;
  auto value=values.find(key);
  if(value!=values.end()){if(written.insert(key).second)output<<key<<' '<<value->second<<'\n';}
  else output<<line<<'\n';
 }
 for(const auto& value:values)if(!written.count(value.first))output<<value.first<<' '<<value.second<<'\n';
 return output.str();
}
}
