/*
    This file is part of auditlog2db.

    auditlog2db is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    auditlog2db is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with auditlog2db.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <string>
#include <cstring>
#include <sqlite3.h>
#include <vector>
#include <fstream>
#include <boost/regex.hpp>
#include <chrono>

#include <time.h>
#include <sstream> // for converting time_t to str

// standard library header for ordered map
#include <unordered_map>
#include <get_unordered_map.h> // part of this program



using namespace std;
using std::vector;
using std::unordered_map;

using std::string;
//using std::sstream;
using std::stringstream;

// convert Apache log time to unix time using this function http://www.thejach.com/view/2012/7/apaches_common_log_format_datetime_converted_to_unix_timestamp_with_c
//#include <string>

/*
 * Parses apache logtime into tm, converts to time_t, and reformats to str.
 * logtime should be the format: day/month/year:hour:minute:second zone
 * day = 2*digit
 * month = 3*letter
 * year = 4*digit
 * hour = 2*digit
 * minute = 2*digit
 * second = 2*digit
 * zone = (`+' | `-') 4*digit
 *
 * e.g. 04/Apr/2012:10:37:29 -0500
 */
string logtimeToUnix(const string& logtime) {
  struct tm tm;
  time_t t;
  if (strptime(logtime.c_str(), "%d/%b/%Y:%H:%M:%S %Z", &tm) == NULL)
    return "-";
  
  tm.tm_isdst = 0; // Force dst off
  // Parse the timezone, the five digits start with the sign at idx 21.
  int hours = 10*(logtime[22] - '0') + logtime[23] - '0';
  int mins = 10*(logtime[24] - '0') + logtime[25] - '0';
  int off_secs = 60*60*hours + 60*mins;
  if (logtime[21] == '-')
    off_secs *= -1;

  t = mktime(&tm);
  if (t == -1)
    return "-";
  t -= timezone; // Local timezone
  t += off_secs;

  string retval;
  stringstream stream;
  stream << t;
  stream >> retval;
  return retval;
}








// function to return an ID (used as the primary key in the database) from the C++ map (which is a reverse-map of the database i.e. the key is the value, and the value is the ID)
// using a reference (&) to the unordered_map so that the changes made to the map inside the function are not lost
int ID_from_map(string key, unordered_map<string, int>& mymap, int debug) {
    
    if (key == "") { // if the key is an empty string, return 0 - the bind_ID function will check for this later and not bind anything, resulting in NULL in the database 
        return 0;
    }
    
    auto iterator = mymap.find(key);
    if (iterator == mymap.end()) { // if the key does not exist in the map
        
        if(debug) {cout << "adding key " << key << " to the map";}
        
        // we need to know what the highest ID in the map is before we insert a new pair
        int maxID = 1;
        
        for (auto &it : mymap) { // iterate through map and find highest ID
            if (it.second >= maxID) {
                maxID = it.second + 1; // new ID must be greater than all other IDs in the map
            }
        }
        if(debug) {cout << ", ID is " << maxID << endl;}
        
        mymap.insert({key,maxID}); // add new key & value pair to the map
        return maxID; // return the ID value for the key we just added
        
    } else {
        if(debug){cout << "found key " << key << " in the map, ID is " << iterator->second << endl;}
        return iterator->second;
    }
}







void commit_maps(sqlite3 *db, const char *sql, unordered_map<string, int>& mymap, int debug) {
    
    //sql something like "INSERT INTO table (something_ID, something) VALUES (:id, :value);";
    
    // prepare sql statement
    sqlite3_stmt *stmt;
    const char *pzTail;
    
    int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, &pzTail);
    if( rc != SQLITE_OK ){
      cerr << "SQL error compiling the prepared statement" << endl;
      cerr << "The error was: "<< sqlite3_errmsg(db) << endl;
    } else {
      if (debug) {cout << "Prepared statement was compiled successfully" << endl;}
    }
    
    
    
    
    for (auto &it : mymap) {
        
        // print data in map for debugging
        if (debug) {cout << "Key: " << it.first << " Value: " << it.second << endl;}
        
        // bind variables        
        sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), it.second);
        sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":value"), it.first.c_str(), it.first.length(), 0);
        
        // step statement and report errors, if any
        int step_rc = sqlite3_step(stmt);
        if (step_rc != SQLITE_OK && step_rc != SQLITE_DONE) {
            cerr << "SQLite error stepping statement at key " << it.first << " value " << it.second << " . Code " << sqlite3_errcode(db) << ": " << sqlite3_errmsg(db) << endl;
        } else {
            if (debug) {cout << "Statement was stepped successfully" << endl;}
	}
	
	// reset statement
	int rc = sqlite3_reset(stmt);
	if( rc != SQLITE_OK ){
            cerr << "SQL error resetting the prepared statement, the error was: "<< sqlite3_errmsg(db) << endl;
        } else {
            if (debug) {cout << "Prepared statement was reset successfully" << endl;}
        }
        
        // clear variables        
        rc = sqlite3_clear_bindings(stmt);
        if( rc != SQLITE_OK ){
            cerr << "SQL error clearing the bindings, the error was: "<< sqlite3_errmsg(db) << endl;
        } else {
            if (debug) {cout << "Bindings were cleared successfully" << endl;}
        }
        
        
        
    }
    
    sqlite3_finalize(stmt);
}








// function to bind an ID to a statement if the ID is not 0
void bind_ID (sqlite3_stmt *stmt, const char * colonidstring, int ID, int debug) {
    // check if the ID is zero. If it is, don't bind anything (no data, default value in database is NULL)
    if (ID == 0) {
        if (debug) {cout << "ID integer is zero, not binding anything" << endl;}
    } else {
        if (debug) {cout << "Binding ID" << endl;}
        sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, colonidstring), ID);
    } 
}


              
              
              
map <string, pair<string,int>> ruledata (string ruledatafile, int debug) {
    
    if (debug) {cout << "Rule data file is " << ruledatafile << endl;}
    if (debug) {cout << "Generating ruledata map" << endl;}
    
    map <string, pair<string, int>> results;
    
    // search through modsecurity log file for line numbers of headers, save them along with the line numbers they appear on
    boost::regex ruledataregex("^(\\d{6})\\s*(\\w+)\\s*(\\d).*$");
  
    int line = 0;
    string str;
    ifstream in(ruledatafile);
    boost::cmatch matches;


    while (getline(in, str)) {
        ++line;
        // if the regex matches, add to the map
        if (boost::regex_match(str.c_str(), matches, ruledataregex)) {
            //matches[0] contains the original string. matches[n] contains a submatch for each matching subexpression
            if (debug) { cout << "match on line " << line << " : " << matches[0] << endl;}
            string ruleno = matches[1];
            string rulefile = matches[2];
            string scorestring = matches[3];
            int score = atoi(scorestring.c_str());
            
            if (debug) {cout << "rule number is: " << ruleno << " rule file is: " << rulefile << " score is: " << score << endl;}
      
            // insert a new key into the map, value is a pair containing the rulefile and source
            results.insert({ruleno,make_pair(rulefile,score)});
            //prepared_statements_map.insert({"sql_insert_crs_ip_forensics",make_tuple(sql_insert_crs_ip_forensics, &stmt_insert_crs_ip_forensics)});
        } else {
            if (debug) {cout << "No match on line " << line << ", data: " << str << endl;}
        }
    }
    
    return results;
}
              
              

              
              
              
              
              
              
              
              
              
// 1. get size of the vector holding the header strings and line numbers
// 2. perform queries on the database to get information about data already present, so that we can use the same IDs for matches in this log file 
// 3. start on vector row 1. determine the header letter type
// 4. get row line number for current header and row number for next header
// 5. read file, when the line number is >= the current header number and < the next header number, append the line to the data string
// 6. commit string to the correct column in the 'main' table in the database
// 7. use regular expressions to match important parts of this header (e.g. source IP); populate the table for this header with IDs mapping to each unique match, write map of IDs to matches in a separate table   
// 8. move on to next row in results vector              

int logchop(string database, string logfile, string rulesdatafile, vector<pair<int,string>> results, int debug, int force) {
  // set a timer
  std::chrono::time_point<std::chrono::system_clock> start, end;
  start = std::chrono::system_clock::now();
  
  // record counter
  int recordCounter = 0;
  
  
  // 1. get size of the vector holding the header strings and line numbers
  // always two columns because each element in the vector is a pair
  int rows = results.size(); 
  

  // open database
  sqlite3 *db;
  int rc = sqlite3_open(database.c_str(), &db);
  if(rc) {
    cerr << "Can't open database" << endl;
  } else {
    if (debug) {cout << "Opened database successfully" << endl;}
  } 

  char *zErrMsg = 0;
  
  
  
  // 2. perform queries on the database to get information about data already present, so that we can use the same IDs for matches in this log file
  
  // A
  // - timestamp
  std::unordered_map<string, int> source_ip_map = get_unordered_map(database,"SELECT source_ip_id, source_ip FROM source_ip;",debug);
  std::unordered_map<string, int> source_port_map = get_unordered_map(database,"SELECT source_port_id, source_port FROM source_port;",debug);
  std::unordered_map<string, int> destination_ip_map = get_unordered_map(database,"SELECT destination_ip_id, destination_ip FROM destination_ip;",debug);
  std::unordered_map<string, int> destination_port_map = get_unordered_map(database,"SELECT destination_port_id, destination_port FROM destination_port;",debug);
  
  // B
  std::unordered_map<string, int> request_method_map = get_unordered_map(database,"SELECT request_method_id, request_method FROM request_method;",debug);
  std::unordered_map<string, int> uri_map = get_unordered_map(database,"SELECT uri_id, uri FROM uri;",debug);
  std::unordered_map<string, int> http_version_b_map = get_unordered_map(database,"SELECT http_version_b_id, http_version_b FROM http_version_b;",debug);
  std::unordered_map<string, int> hosts_map = get_unordered_map(database,"SELECT host_id, host FROM hosts;",debug);
  std::unordered_map<string, int> connection_b_map = get_unordered_map(database,"SELECT connection_b_id, connection_b FROM connection_b;",debug);
  std::unordered_map<string, int> accept_map = get_unordered_map(database,"SELECT accept_id, accept FROM accept;",debug);
  std::unordered_map<string, int> user_agent_map = get_unordered_map(database,"SELECT user_agent_id, user_agent FROM user_agent;",debug);
  std::unordered_map<string, int> dnt_map = get_unordered_map(database,"SELECT dnt_id, dnt FROM dnt;",debug);
  std::unordered_map<string, int> referrer_map = get_unordered_map(database,"SELECT referrer_id, referrer FROM referrer;",debug);
  std::unordered_map<string, int> accept_encoding_map = get_unordered_map(database,"SELECT accept_encoding_id, accept_encoding FROM accept_encoding;",debug);
  std::unordered_map<string, int> accept_language_map = get_unordered_map(database,"SELECT accept_language_id, accept_language FROM accept_language;",debug);
  std::unordered_map<string, int> cookie_map = get_unordered_map(database,"SELECT cookie_id, cookie FROM cookie;",debug);
  std::unordered_map<string, int> x_requested_with_map = get_unordered_map(database,"SELECT x_requested_with_id, x_requested_with FROM x_requested_with;",debug);
  std::unordered_map<string, int> content_type_b_map = get_unordered_map(database,"SELECT content_type_b_id, content_type_b FROM content_type_b;",debug);
  std::unordered_map<string, int> content_length_b_map = get_unordered_map(database,"SELECT content_length_b_id, content_length_b FROM content_length_b;",debug);
  std::unordered_map<string, int> proxy_connection_map = get_unordered_map(database,"SELECT proxy_connection_id, proxy_connection FROM proxy_connection;",debug);
  std::unordered_map<string, int> accept_charset_map = get_unordered_map(database,"SELECT accept_charset_id, accept_charset FROM accept_charset;",debug);
  std::unordered_map<string, int> ua_cpu_map = get_unordered_map(database,"SELECT ua_cpu_id, ua_cpu FROM ua_cpu;",debug);
  std::unordered_map<string, int> x_forwarded_for_map = get_unordered_map(database,"SELECT x_forwarded_for_id, x_forwarded_for FROM x_forwarded_for;",debug);
  std::unordered_map<string, int> cache_control_b_map = get_unordered_map(database,"SELECT cache_control_b_id, cache_control_b FROM cache_control_b;",debug);
  std::unordered_map<string, int> via_map = get_unordered_map(database,"SELECT via_id, via FROM via;",debug);
  std::unordered_map<string, int> if_modified_since_map = get_unordered_map(database,"SELECT if_modified_since_id, if_modified_since FROM if_modified_since;",debug);
  std::unordered_map<string, int> if_none_match_map = get_unordered_map(database,"SELECT if_none_match_id, if_none_match FROM if_none_match;",debug);
  std::unordered_map<string, int> pragma_b_map = get_unordered_map(database,"SELECT pragma_b_id, pragma_b FROM pragma_b;",debug);
  
  // F
  std::unordered_map<string, int> http_version_f_map = get_unordered_map(database,"SELECT http_version_f_id, http_version_f FROM http_version_f;",debug);
  std::unordered_map<string, int> http_status_code_map = get_unordered_map(database,"SELECT http_status_code_id, http_status_code FROM http_status_code;",debug);
  std::unordered_map<string, int> http_status_text_map = get_unordered_map(database,"SELECT http_status_text_id, http_status_text FROM http_status_text;",debug);
  std::unordered_map<string, int> x_powered_by_map = get_unordered_map(database,"SELECT x_powered_by_id, x_powered_by FROM x_powered_by;",debug);
  std::unordered_map<string, int> expires_map = get_unordered_map(database,"SELECT expires_id, expires FROM expires;",debug);
  std::unordered_map<string, int> cache_control_f_map = get_unordered_map(database,"SELECT cache_control_f_id, cache_control_f FROM cache_control_f;",debug);
  std::unordered_map<string, int> pragma_f_map = get_unordered_map(database,"SELECT pragma_f_id, pragma_f FROM pragma_f;",debug);
  std::unordered_map<string, int> vary_map = get_unordered_map(database,"SELECT vary_id, vary FROM vary;",debug);
  std::unordered_map<string, int> content_encoding_map = get_unordered_map(database,"SELECT content_encoding_id, content_encoding FROM content_encoding;",debug);
  std::unordered_map<string, int> content_length_f_map = get_unordered_map(database,"SELECT content_length_f_id, content_length_f FROM content_length_f;",debug);
  std::unordered_map<string, int> connection_f_map = get_unordered_map(database,"SELECT connection_f_id, connection_f FROM connection_f;",debug);
  std::unordered_map<string, int> content_type_f_map = get_unordered_map(database,"SELECT content_type_f_id, content_type_f FROM content_type_f;",debug);
  std::unordered_map<string, int> status_map = get_unordered_map(database,"SELECT status_id, status FROM status;",debug);
  std::unordered_map<string, int> keep_alive_map = get_unordered_map(database,"SELECT keep_alive_id, keep_alive FROM keep_alive;",debug);
  
  // H
  std::unordered_map<string, int> messages_map = get_unordered_map(database,"SELECT messages_id, messages FROM messages;",debug);
  std::unordered_map<string, int> apache_handler_map = get_unordered_map(database,"SELECT apache_handler_id, apache_handler FROM apache_handler;",debug);
  // - stopwatch
  // - stopwatch2
  std::unordered_map<string, int> producer_map = get_unordered_map(database,"SELECT producer_id, producer FROM producer;",debug);
  std::unordered_map<string, int> server_map = get_unordered_map(database,"SELECT server_id, server FROM server;",debug);
  std::unordered_map<string, int> engine_mode_map = get_unordered_map(database,"SELECT engine_mode_id, engine_mode FROM engine_mode;",debug);
  std::unordered_map<string, int> action_map = get_unordered_map(database,"SELECT action_id, action FROM action;",debug);
  std::unordered_map<string, int> apache_error_map = get_unordered_map(database,"SELECT apache_error_id, apache_error FROM apache_error;",debug);
  std::unordered_map<string, int> xml_parser_error_map = get_unordered_map(database,"SELECT xml_parser_error_id, xml_parser_error FROM xml_parser_error;",debug);
  
  
  
  //debug = 1;
  // WALRUS
  cout << "Generating rule data map from the rulesdata file " << rulesdatafile; 
  map <string, pair<string,int>> ruledatamap = ruledata (rulesdatafile, debug);
  cout << " ...done" << endl;
  //debug = 0;
  
  
  // generate a map from the rule filename string to a counter
  
  map <string, int> rulefiletocountermap;
  
  for (const auto &iterator : ruledatamap) {
      // add the current rule file string to the set 
      
      //cout << (iterator.second).first << endl;
      //cout << (iterator.second).second << endl;
      int foo;
      string ruledatafile = (iterator.second).first;
      rulefiletocountermap.insert ({ruledatafile, foo});
  }
  
  // print the map
  
  if (debug) {
      for (const auto &iterator : rulefiletocountermap) {
          cout << "Rule file " << iterator.first << " maps to integer " << iterator.second << endl; 
      }
  }
  
  
  
  
  
  
  
  
  
  
  
  // stuff for boost regex matching
  boost::cmatch match; // cmatch type to hold matches
  
  // matches for section A, example data:
  // [25/Feb/2014:14:00:43 +0000] UwyiC38AAQEAAEx4slsAAAAG 125.210.204.242 40996 192.168.1.103 80
  boost::regex A_regex("^\\[(.*)\\]\\s(.{24})\\s(\\d+\\.\\d+\\.\\d+\\.\\d+)\\s(\\d+)\\s(\\d+\\.\\d+\\.\\d+\\.\\d+)\\s(\\d+).*"); // 1st match is TIMESTAMP, 2nd match is APACHE_UID, 3rd match is SOURCE_IP, 4th match is SOURCE_PORT, 5th match is DESTINATION_IP, 6th match is DESTINATION_PORT
  
  

  
  
  // matches for section B (request headers)
  boost::regex B_regex("^(\\w+)\\s(.*)\\s(HTTP\\/\\d\\.\\d).*"); // 1st match is request method, 2nd match is URI, 3rd match is HTTP version
  boost::regex B_regex_host("^Host:(.*?)$");
  boost::regex B_regex_connection("^Connection:(.*?)$");
  boost::regex B_regex_accept("^Accept:(.*?)$");
  boost::regex B_regex_useragent("^User-Agent:(.*?)$"); // match for user agent string for use with regex_search
  boost::regex B_regex_DNT("^DNT:(.*?)$");
  boost::regex B_regex_referrer("^Referrer:(.*?)$");
  boost::regex B_regex_accept_encoding("^Accept-Encoding:(.*?)$");
  boost::regex B_regex_accept_language("^Accept-Language:(.*?)$");
  boost::regex B_regex_cookie("^Cookie:(.*?)$");
  boost::regex B_regex_x_requested_with("^X-Requested-With:(.*?)$");
  boost::regex B_regex_content_type("^Content-Type:(.*?)$");
  boost::regex B_regex_content_length("^Content-Length:(.*?)$");
  boost::regex B_regex_proxy_connection("^Proxy-Connection:(.*?)$");
  boost::regex B_regex_accept_charset("^Accept-Charset:(.*?)$");
  boost::regex B_regex_UA_CPU("^UA-CPU:(.*?)$");
  boost::regex B_regex_x_forwarded_for("^X-Forwarded-For:(.*?)$");
  boost::regex B_regex_cache_control("^Cache-Control:(.*?)$");
  boost::regex B_regex_via("^Via:(.*?)$");
  boost::regex B_regex_if_modified_since("^If-Modified-Since:(.*?)$");
  boost::regex B_regex_if_none_match("^If-None-Match:(.*?)$");
  boost::regex B_regex_pragma("^Pragma:(.*?)$");
  
  
  // matches for section C (request body)
  // (none)
  
  // section D not implemented by modsecurity
  // (none)
  
  // matches for section E (intermediary response body)
  // (none)
  
  // matches for section F (final response headers)
  boost::regex F_regex("^(HTTP\\/\\d\\.\\d)\\s(\\d+)\\s(.*?)$"); // 1st match is HTTP version, 2nd match is HTTP code, 3rd match is HTTP code description
  boost::regex F_regex_x_powered_by("^X-Powered-By:(.*?)$");
  boost::regex F_regex_expires("^Expires:(.*?)$");
  boost::regex F_regex_cache_control("^Cache-Control:(.*?)$");
  boost::regex F_regex_pragma("^Pragma:(.*?)$");
  boost::regex F_regex_vary("^Vary:(.*?)$");
  boost::regex F_regex_content_encoding("^Content-Encoding:(.*?)$");
  boost::regex F_regex_content_length("^Content-Length:(.*?)$");
  boost::regex F_regex_connection("^Connection:(.*?)$");
  boost::regex F_regex_content_type("^Content-Type:(.*?)$");
  boost::regex F_regex_status("^Status:(.*?)$");
  boost::regex F_regex_keep_alive("^Keep-Alive:(.*?)$");
  
  // section G not implemented by modsecurity
  // (none)
  
  // matches for section H (audit log trailer)
  boost::regex H_regex_messages("^Message:(.*?)$");
  boost::regex H_regex_apache_handler("^Apache-Handler:(.*?)$");
  boost::regex H_regex_apache_error("^Apache-Error:(.*?)$");
  boost::regex H_regex_stopwatch("^Stopwatch:(.*?)$");
  boost::regex H_regex_stopwatch2("^Stopwatch2:(.*?)$");
  //boost::regex H_regex_response_body_transformed("^Apache-Handler:(.*?)$");
  boost::regex H_regex_producer("^Producer:(.*?)$");
  boost::regex H_regex_server("^Server:(.*?)$");
  boost::regex H_regex_engine_mode("^Engine-Mode:\\s\"(.*?)\"$");
  boost::regex H_regex_action("^Action:(.*?)$");
  boost::regex H_regex_xml_parser_error("^Message: XML parser error:(.*?)$");
  
  // matches for any rule ID
  boost::regex H_regex_any_rule("\\[id\\s\"(\\d{6})\"\\]");
  
  
  // matches for section I (a replacement for part C)
  // (none)
  
  // matches for section J (contains information about files uploaded using multipart/form-data encoding)
  // (none)
  
  // matches for section K (list of every rule that matched, one per line, in the order they were matched)
  // (none)
  
  // create the SQL statements that can be used to commit the values to the database
  map <string, tuple<const char *, sqlite3_stmt **>> prepared_statements_map;
  
  
  // NB: unbound values in prepared statements are NULL
  const char *sql_insert_main = "INSERT INTO main (UNIQUE_ID, HEADER, A, B, C, D, E, F, G, H, I, J, K) VALUES (:UNIQUE_ID, :HEADER, :A, :B, :C, :D, :E, :F, :G, :H, :I, :J, :K);";
  sqlite3_stmt *stmt_insert_main; // compiled statement handle (pointer of type sqlite3_stmt)
  prepared_statements_map.insert({"sql_insert_main",	make_tuple(sql_insert_main, &stmt_insert_main)});
  
  const char *sql_insert_A = "INSERT INTO A (UNIQUE_ID, TIMESTAMP, UNIXTIME, SOURCE_IP_ID, SOURCE_PORT_ID, DESTINATION_IP_ID, DESTINATION_PORT_ID) VALUES (:UNIQUE_ID, :TIMESTAMP, :UNIXTIME, :SOURCE_IP_ID, :SOURCE_PORT_ID, :DESTINATION_IP_ID, :DESTINATION_PORT_ID);";
  sqlite3_stmt *stmt_insert_A;
  prepared_statements_map.insert({"sql_insert_A", make_tuple(sql_insert_A, &stmt_insert_A)});
  
  const char *sql_insert_B = "INSERT INTO B (UNIQUE_ID, REQUEST_METHOD_ID, URI_ID, HTTP_VERSION_ID, HOST_ID, CONNECTION_ID, ACCEPT_ID, USER_AGENT_ID, DNT_ID, REFERRER_ID, ACCEPT_ENCODING_ID, ACCEPT_LANGUAGE_ID, COOKIE_ID, X_REQUESTED_WITH_ID, CONTENT_TYPE_ID, CONTENT_LENGTH_ID, PROXY_CONNECTION_ID, ACCEPT_CHARSET_ID, UA_CPU_ID, X_FORWARDED_FOR_ID, CACHE_CONTROL_ID, VIA_ID, IF_MODIFIED_SINCE_ID, IF_NONE_MATCH_ID, PRAGMA_ID) VALUES (:UNIQUE_ID, :REQUEST_METHOD_ID, :REQUEST_URI_ID, :REQUEST_HTTP_VERSION_ID, :REQUEST_HOST_ID, :REQUEST_CONNECTION_ID, :REQUEST_ACCEPT_ID, :REQUEST_USER_AGENT_ID, :REQUEST_DNT_ID, :REQUEST_REFERRER_ID, :REQUEST_ACCEPT_ENCODING_ID, :REQUEST_ACCEPT_LANGUAGE_ID, :REQUEST_COOKIE_ID, :REQUEST_X_REQUESTED_WITH_ID, :REQUEST_CONTENT_TYPE_ID, :REQUEST_CONTENT_LENGTH_ID, :REQUEST_PROXY_CONNECTION_ID, :REQUEST_ACCEPT_CHARSET_ID, :REQUEST_UA_CPU_ID, :REQUEST_X_FORWARDED_FOR_ID, :REQUEST_CACHE_CONTROL_ID, :REQUEST_VIA_ID, :REQUEST_IF_MODIFIED_SINCE_ID, :REQUEST_IF_NONE_MATCH_ID, :REQUEST_PRAGMA_ID);";
  sqlite3_stmt *stmt_insert_B;
  prepared_statements_map.insert({"sql_insert_B", make_tuple(sql_insert_B, &stmt_insert_B)});
  
  const char *sql_insert_F = "INSERT INTO F (UNIQUE_ID, HTTP_VERSION_ID, HTTP_STATUS_CODE_ID, HTTP_STATUS_TEXT_ID, X_POWERED_BY_ID, EXPIRES_ID, CACHE_CONTROL_ID, PRAGMA_ID, VARY_ID, CONTENT_ENCODING_ID, CONTENT_LENGTH_ID, CONNECTION_ID, CONTENT_TYPE_ID, STATUS_ID, KEEP_ALIVE_ID) VALUES (:UNIQUE_ID, :RESPONSE_HTTP_VERSION_ID, :RESPONSE_HTTP_STATUS_CODE_ID, :RESPONSE_HTTP_STATUS_TEXT_ID, :RESPONSE_X_POWERED_BY_ID, :RESPONSE_EXPIRES_ID, :RESPONSE_CACHE_CONTROL_ID, :RESPONSE_PRAGMA_ID, :RESPONSE_VARY_ID, :RESPONSE_CONTENT_ENCODING_ID, :RESPONSE_CONTENT_LENGTH_ID, :RESPONSE_CONNECTION_ID, :RESPONSE_CONTENT_TYPE_ID, :RESPONSE_STATUS_ID, :RESPONSE_KEEP_ALIVE_ID);";
  sqlite3_stmt *stmt_insert_F;
  prepared_statements_map.insert({"sql_insert_F",make_tuple(sql_insert_F, &stmt_insert_F)});
  
  // messages, engine mode
  const char *sql_insert_H = "INSERT INTO H (UNIQUE_ID, MESSAGES_ID, APACHE_HANDLER_ID, APACHE_ERROR_ID, STOPWATCH, STOPWATCH2, PRODUCER_ID, SERVER_ID, ENGINE_MODE_ID, ACTION_ID, XML_PARSER_ERROR_ID) VALUES (:UNIQUE_ID, :TRAILER_MESSAGES_ID, :TRAILER_APACHE_HANDLER_ID, :TRAILER_APACHE_ERROR_ID, :TRAILER_STOPWATCH, :TRAILER_STOPWATCH2, :TRAILER_PRODUCER_ID, :TRAILER_SERVER_ID, :TRAILER_ENGINE_MODE_ID, :TRAILER_ACTION_ID, :TRAILER_XML_PARSER_ERROR_ID);";
  sqlite3_stmt *stmt_insert_H;
  prepared_statements_map.insert({"sql_insert_H",make_tuple(sql_insert_H, &stmt_insert_H)});

  
  
  // ************************* PROTOCOL VIOLATION **************************  
  const char *sql_insert_crs_protocol_violation = "INSERT INTO CRS_20_PROTOCOL_VIOLATIONS (UNIQUE_ID, '960911', '981227', '960000', '960912', '960914', '960915','960016','960011','960012','960902','960022','960020','958291','958230','958231','958295','950107','950109','950108','950801','950116','960014','960901','960018') VALUES (:UNIQUE_ID, :960911, :981227, :960000, :960912, :960914, :960915,:960016,:960011,:960012,:960902,:960022,:960020,:958291,:958230,:958231,:958295,:950107,:950109,:950108,:950801,:950116,:960014,:960901,:960018);";
  sqlite3_stmt *stmt_insert_crs_protocol_violation;
  prepared_statements_map.insert({"sql_insert_crs_protocol_violation",make_tuple(sql_insert_crs_protocol_violation, &stmt_insert_crs_protocol_violation)});
  
  // ************************* PROTOCOL ANOMALY **************************
  const char *sql_insert_crs_protocol_anomaly = "INSERT INTO CRS_21_PROTOCOL_ANOMALIES (UNIQUE_ID, '960008', '960007', '960015', '960021', '960009', '960006', '960904', '960017') VALUES (:UNIQUE_ID, :960008, :960007, :960015, :960021, :960009, :960006, :960904, :960017);";
  sqlite3_stmt *stmt_insert_crs_protocol_anomaly;
  prepared_statements_map.insert({"sql_insert_protocol_anomaly",make_tuple(sql_insert_crs_protocol_anomaly, &stmt_insert_crs_protocol_anomaly)});
  
  // ************************* REQUEST LIMIT **************************
  const char *sql_insert_crs_request_limit = "INSERT INTO CRS_23_REQUEST_LIMITS (UNIQUE_ID, '960209', '960208', '960335', '960341', '960342', '960343') VALUES (:UNIQUE_ID, :960209, :960208, :960335, :960341, :960342, :960343);";
  sqlite3_stmt *stmt_insert_crs_request_limit;
  prepared_statements_map.insert({"sql_insert_crs_request_limit",make_tuple(sql_insert_crs_request_limit, &stmt_insert_crs_request_limit)});
  
  // ************************* HTTP POLICY **************************
  const char *sql_insert_crs_http_policy = "INSERT INTO CRS_30_HTTP_POLICY (UNIQUE_ID, '960032', '960010', '960034', '960035', '960038') VALUES (:UNIQUE_ID, :960032, :960010, :960034, :960035, :960038);";
  sqlite3_stmt *stmt_insert_crs_http_policy;
  prepared_statements_map.insert({"sql_insert_crs_http_policy",make_tuple(sql_insert_crs_http_policy, &stmt_insert_crs_http_policy)});
  
  // ************************* BAD ROBOT **************************
  const char *sql_insert_crs_bad_robot = "INSERT INTO CRS_35_BAD_ROBOTS (UNIQUE_ID, '990002', '990901', '990902', '990012') VALUES (:UNIQUE_ID, :990002, :990901, :990902, :990012);";
  sqlite3_stmt *stmt_insert_crs_bad_robot;
  prepared_statements_map.insert({"sql_insert_crs_bad_robot",make_tuple(sql_insert_crs_bad_robot, &stmt_insert_crs_bad_robot)});
  
  // ************************* GENERIC ATTACK **************************
  const char *sql_insert_crs_generic_attack = "INSERT INTO CRS_40_GENERIC_ATTACKS (UNIQUE_ID, '950907', '960024', '950008', '950010', '950011', '950018', '950019', '950012', '950910', '950911', '950117', '950118', '950119', '950120', '981133', '950009', '950003', '950000', '950005', '950002', '950006', '959151', '958976', '958977') VALUES (:UNIQUE_ID, :950907, :960024, :950008, :950010, :950011, :950018, :950019, :950012, :950910, :950911, :950117, :950118, :950119, :950120, :981133, :950009, :950003, :950000, :950005, :950002, :950006, :959151, :958976, :958977);";
  sqlite3_stmt *stmt_insert_crs_generic_attack;
  prepared_statements_map.insert({"sql_insert_crs_generic_attack",make_tuple(sql_insert_crs_generic_attack, &stmt_insert_crs_generic_attack)});

  // ************************* SQL INJECTION ATTACK **************************
  const char *sql_insert_crs_sql_injection = "INSERT INTO CRS_41_SQL_INJECTION_ATTACKS (UNIQUE_ID, '981231', '981260', '981318', '981319', '950901', '981320', '981300', '981301', '981302', '981303', '981304', '981305', '981306', '981307', '981308', '981309', '981310', '981311', '981312', '981313', '981314', '981315', '981316', '981317', '950007', '950001', '959070', '959071', '959072', '950908', '959073', '981172', '981173', '981272', '981244', '981255', '981257', '981248', '981277', '981250', '981241', '981252', '981256', '981245', '981276', '981254', '981270', '981240', '981249', '981253', '981242', '981246', '981251', '981247', '981243') VALUES (:UNIQUE_ID, :981231, :981260, :981318, :981319, :950901, :981320, :981300, :981301, :981302, :981303, :981304, :981305, :981306, :981307, :981308, :981309, :981310, :981311, :981312, :981313, :981314, :981315, :981316, :981317, :950007, :950001, :959070, :959071, :959072, :950908, :959073, :981172, :981173, :981272, :981244, :981255, :981257, :981248, :981277, :981250, :981241, :981252, :981256, :981245, :981276, :981254, :981270, :981240, :981249, :981253, :981242, :981246, :981251, :981247, :981243);";
  sqlite3_stmt *stmt_insert_crs_sql_injection;
  prepared_statements_map.insert({"sql_insert_crs_sql_injection",make_tuple(sql_insert_crs_sql_injection, &stmt_insert_crs_sql_injection)});
  
  // ************************* XSS ATTACK **************************
  const char *sql_insert_crs_xss_attack = "INSERT INTO CRS_41_XSS_ATTACKS (UNIQUE_ID, '973336', '973337', '973338', '981136', '981018', '958016', '958414', '958032', '958026', '958027', '958054', '958418', '958034', '958019', '958013', '958408', '958012', '958423', '958002', '958017', '958007', '958047', '958410', '958415', '958022', '958405', '958419', '958028', '958057', '958031', '958006', '958033', '958038', '958409', '958001', '958005', '958404', '958023', '958010', '958411', '958422', '958036', '958000', '958018', '958406', '958040', '958052', '958037', '958049', '958030', '958041', '958416', '958024', '958059', '958417', '958020', '958045', '958004', '958421', '958009', '958025', '958413', '958051', '958420', '958407', '958056', '958011', '958412', '958008', '958046', '958039', '958003', '973300', '973301', '973302', '973303', '973304', '973305', '973306', '973307', '973308', '973309', '973310', '973311', '973312', '973313', '973314', '973331', '973315', '973330', '973327', '973326', '973346', '973345', '973324', '973323', '973322', '973348', '973321', '973320', '973318', '973317', '973347', '973335', '973334', '973333', '973344', '973332', '973329', '973328', '973316', '973325', '973319') VALUES (:UNIQUE_ID, :973336, :973337, :973338, :981136, :981018, :958016, :958414, :958032, :958026, :958027, :958054, :958418, :958034, :958019, :958013, :958408, :958012, :958423, :958002, :958017, :958007, :958047, :958410, :958415, :958022, :958405, :958419, :958028, :958057, :958031, :958006, :958033, :958038, :958409, :958001, :958005, :958404, :958023, :958010, :958411, :958422, :958036, :958000, :958018, :958406, :958040, :958052, :958037, :958049, :958030, :958041, :958416, :958024, :958059, :958417, :958020, :958045, :958004, :958421, :958009, :958025, :958413, :958051, :958420, :958407, :958056, :958011, :958412, :958008, :958046, :958039, :958003, :973300, :973301, :973302, :973303, :973304, :973305, :973306, :973307, :973308, :973309, :973310, :973311, :973312, :973313, :973314, :973331, :973315, :973330, :973327, :973326, :973346, :973345, :973324, :973323, :973322, :973348, :973321, :973320, :973318, :973317, :973347, :973335, :973334, :973333, :973344, :973332, :973329, :973328, :973316, :973325, :973319);";
  sqlite3_stmt *stmt_insert_crs_xss_attack;
  prepared_statements_map.insert({"sql_insert_crs_xss_attack",make_tuple(sql_insert_crs_xss_attack, &stmt_insert_crs_xss_attack)});

  // ************************* TIGHT SECURITY **************************
  const char *sql_insert_crs_tight_security = "INSERT INTO CRS_42_TIGHT_SECURITY (UNIQUE_ID, '950103') VALUES (:UNIQUE_ID, :950103);";
  sqlite3_stmt *stmt_insert_crs_tight_security;
  prepared_statements_map.insert({"sql_insert_crs_tight_security",make_tuple(sql_insert_crs_tight_security, &stmt_insert_crs_tight_security)});

  // ************************* TROJANS **************************
  const char *sql_insert_crs_trojans = "INSERT INTO CRS_45_TROJANS (UNIQUE_ID, '950110', '950921', '950922') VALUES (:UNIQUE_ID, :950110, :950921, :950922);";
  sqlite3_stmt *stmt_insert_crs_trojans;
  prepared_statements_map.insert({"sql_insert_crs_trojans",make_tuple(sql_insert_crs_trojans, &stmt_insert_crs_trojans)});

  // ************************* COMMON EXCEPTIONS **************************
  const char *sql_insert_crs_common_exceptions = "INSERT INTO CRS_47_COMMON_EXCEPTIONS (UNIQUE_ID, '981020', '981021', '981022') VALUES (:UNIQUE_ID, :981020, :981021, :981022);";
  sqlite3_stmt *stmt_insert_crs_common_exceptions;
  prepared_statements_map.insert({"sql_insert_crs_common_exceptions",make_tuple(sql_insert_crs_common_exceptions, &stmt_insert_crs_common_exceptions)});

  // ************************* LOCAL EXCEPTIONS **************************
  const char *sql_insert_crs_local_exceptions = "INSERT INTO CRS_48_LOCAL_EXCEPTIONS (UNIQUE_ID) VALUES (:UNIQUE_ID);";
  sqlite3_stmt *stmt_insert_crs_local_exceptions;
  prepared_statements_map.insert({"sql_insert_crs_local_exceptions",make_tuple(sql_insert_crs_local_exceptions, &stmt_insert_crs_local_exceptions)});

  // ************************* INBOUND BLOCKING **************************
  const char *sql_insert_crs_inbound_blocking = "INSERT INTO CRS_49_INBOUND_BLOCKING (UNIQUE_ID, '981175', '981176') VALUES (:UNIQUE_ID, :981175, :981176);";
  sqlite3_stmt *stmt_insert_crs_inbound_blocking;
  prepared_statements_map.insert({"sql_insert_crs_inbound_blocking",make_tuple(sql_insert_crs_inbound_blocking, &stmt_insert_crs_inbound_blocking)});

  // ************************* OUTBOUND **************************
  const char *sql_insert_crs_outbound = "INSERT INTO CRS_50_OUTBOUND (UNIQUE_ID, '970007', '970008', '970009', '970010', '970012', '970903', '970016', '970018', '970901', '970021', '970011', '981177', '981000', '981001', '981003', '981004', '981005', '981006', '981007', '981178', '970014', '970015', '970902', '970002', '970003', '970004', '970904', '970013') VALUES (:UNIQUE_ID, :970007, :970008, :970009, :970010, :970012, :970903, :970016, :970018, :970901, :970021, :970011, :981177, :981000, :981001, :981003, :981004, :981005, :981006, :981007, :981178, :970014, :970015, :970902, :970002, :970003, :970004, :970904, :970013);";
  sqlite3_stmt *stmt_insert_crs_outbound;
  prepared_statements_map.insert({"sql_insert_crs_outbound",make_tuple(sql_insert_crs_outbound, &stmt_insert_crs_outbound)});

  // ************************* OUTBOUND BLOCKING **************************
  const char *sql_insert_crs_outbound_blocking = "INSERT INTO CRS_59_OUTBOUND_BLOCKING (UNIQUE_ID, '981200') VALUES (:UNIQUE_ID, :981200);";
  sqlite3_stmt *stmt_insert_crs_outbound_blocking;
  prepared_statements_map.insert({"sql_insert_crs_outbound_blocking",make_tuple(sql_insert_crs_outbound_blocking, &stmt_insert_crs_outbound_blocking)});

  // ************************* CORRELATION **************************
  const char *sql_insert_crs_correlation = "INSERT INTO CRS_60_CORRELATION (UNIQUE_ID, '981201', '981202', '981203', '981204', '981205') VALUES (:UNIQUE_ID, :981201, :981202, :981203, :981204, :981205);";
  sqlite3_stmt *stmt_insert_crs_correlation;
  prepared_statements_map.insert({"sql_insert_crs_correlation",make_tuple(sql_insert_crs_correlation, &stmt_insert_crs_correlation)});

  
    
  // ************************* BRUTE FORCE **************************
  const char *sql_insert_crs_brute_force = "INSERT INTO CRS_11_BRUTE_FORCE (UNIQUE_ID, '981036', '981037', '981038', '981039', '981040', '981041', '981042', '981043') VALUES (:UNIQUE_ID, :981036, :981037, :981038, :981039, :981040, :981041, :981042, :981043);";
  sqlite3_stmt *stmt_insert_crs_brute_force;
  prepared_statements_map.insert({"sql_insert_crs_brute_force",make_tuple(sql_insert_crs_brute_force, &stmt_insert_crs_brute_force)});

  // ************************* DOS PROTECTION **************************
  const char *sql_insert_crs_dos = "INSERT INTO CRS_11_DOS_PROTECTION (UNIQUE_ID, '981044', '981045', '981046', '981047', '981048', '981049') VALUES (:UNIQUE_ID, :981044, :981045, :981046, :981047, :981048, :981049);";
  sqlite3_stmt *stmt_insert_crs_dos;
  prepared_statements_map.insert({"sql_insert_crs_dos",make_tuple(sql_insert_crs_dos, &stmt_insert_crs_dos)});

  // ************************* PROXY ABUSE **************************
  const char *sql_insert_crs_proxy_abuse = "INSERT INTO CRS_11_PROXY_ABUSE (UNIQUE_ID, '981050') VALUES (:UNIQUE_ID, :981050);";
  sqlite3_stmt *stmt_insert_crs_proxy_abuse;
  prepared_statements_map.insert({"sql_insert_crs_proxy_abuse",make_tuple(sql_insert_crs_proxy_abuse, &stmt_insert_crs_proxy_abuse)});

  // ************************* SLOW DOS PROTECTION **************************
  const char *sql_insert_crs_slow_dos = "INSERT INTO CRS_11_SLOW_DOS_PROTECTION (UNIQUE_ID, '981051', '981052') VALUES (:UNIQUE_ID, :981051, :981052);";
  sqlite3_stmt *stmt_insert_crs_slow_dos;
  prepared_statements_map.insert({"sql_insert_crs_slow_dos",make_tuple(sql_insert_crs_slow_dos, &stmt_insert_crs_slow_dos)});

  // ************************* CC TRACK PAN **************************
  const char *sql_insert_crs_cc_track_pan = "INSERT INTO CRS_25_CC_TRACK_PAN (UNIQUE_ID, '920021', '920022', '920023') VALUES (:UNIQUE_ID, :920021, :920022, :920023);";
  sqlite3_stmt *stmt_insert_crs_cc_track_pan;
  prepared_statements_map.insert({"sql_insert_crs_cc_track_pan",make_tuple(sql_insert_crs_cc_track_pan, &stmt_insert_crs_cc_track_pan)});

  // ************************* APPSENSOR DETECTION POINT **************************
  const char *sql_insert_crs_appsensor = "INSERT INTO CRS_40_APPSENSOR_DETECTION_POINT (UNIQUE_ID, '981082', '981083', '981084', '981085', '981086', '981087', '981088', '981089', '981090', '981091', '981092', '981093', '981094', '981095', '981096', '981097', '981103', '981104', '981110', '981105', '981098', '981099', '981100', '981101', '981102', '981131', '981132') VALUES (:UNIQUE_ID, :981082, :981083, :981084, :981085, :981086, :981087, :981088, :981089, :981090, :981091, :981092, :981093, :981094, :981095, :981096, :981097, :981103, :981104, :981110, :981105, :981098, :981099, :981100, :981101, :981102, :981131, :981132);";
  sqlite3_stmt *stmt_insert_crs_appsensor;
  prepared_statements_map.insert({"sql_insert_crs_appsensor",make_tuple(sql_insert_crs_appsensor, &stmt_insert_crs_appsensor)});

  // ************************* HTTP PARAMETER POLLUTION **************************
  const char *sql_insert_crs_http_parameter_pollution = "INSERT INTO CRS_40_HTTP_PARAMETER_POLLUTION (UNIQUE_ID, '900032') VALUES (:UNIQUE_ID, :900032);";
  sqlite3_stmt *stmt_insert_crs_http_parameter_pollution;
  prepared_statements_map.insert({"sql_insert_crs_http_parameter_pollution",make_tuple(sql_insert_crs_http_parameter_pollution, &stmt_insert_crs_http_parameter_pollution)});

  // ************************* CSP ENFORCEMENT **************************
  const char *sql_insert_crs_csp_enforcement = "INSERT INTO CRS_42_CSP_ENFORCEMENT (UNIQUE_ID, '981142', '960001', '960002', '960003') VALUES (:UNIQUE_ID, :981142, :960001, :960002, :960003);";
  sqlite3_stmt *stmt_insert_crs_csp_enforcement;
  prepared_statements_map.insert({"sql_insert_crs_csp_enforcement",make_tuple(sql_insert_crs_csp_enforcement, &stmt_insert_crs_csp_enforcement)});

  // ************************* SCANNER INTEGRATION **************************
  const char *sql_insert_crs_scanner_integration = "INSERT INTO CRS_46_SCANNER_INTEGRATION (UNIQUE_ID, '900030', '900031', '999003', '999004') VALUES (:UNIQUE_ID, :900030, :900031, :999003, :999004);";
  sqlite3_stmt *stmt_insert_crs_scanner_integration;
  prepared_statements_map.insert({"sql_insert_crs_scanner_integration",make_tuple(sql_insert_crs_scanner_integration, &stmt_insert_crs_scanner_integration)});

  // ************************* BAYES ANALYSIS **************************
  const char *sql_insert_crs_bayes_analysis = "INSERT INTO CRS_48_BAYES_ANALYSIS (UNIQUE_ID, '900033', '900034', '900035') VALUES (:UNIQUE_ID, :900033, :900034, :900035);";
  sqlite3_stmt *stmt_insert_crs_bayes_analysis;
  prepared_statements_map.insert({"sql_insert_crs_bayes_analysis",make_tuple(sql_insert_crs_bayes_analysis, &stmt_insert_crs_bayes_analysis)});

  // ************************* RESPONSE PROFILING **************************
  const char *sql_insert_crs_response_profiling = "INSERT INTO CRS_55_RESPONSE_PROFILING (UNIQUE_ID, '981187', '981189', '981190', '981191', '981192', '981193', '981194', '981195', '981196', '981197') VALUES (:UNIQUE_ID, :981187, :981189, :981190, :981191, :981192, :981193, :981194, :981195, :981196, :981197);";
  sqlite3_stmt *stmt_insert_crs_response_profiling;
  prepared_statements_map.insert({"sql_insert_crs_response_profiling",make_tuple(sql_insert_crs_response_profiling, &stmt_insert_crs_response_profiling)});

  // ************************* PVI CHECKS **************************
  const char *sql_insert_crs_pvi_checks = "INSERT INTO CRS_56_PVI_CHECKS (UNIQUE_ID, '981198', '981199') VALUES (:UNIQUE_ID, :981198, :981199);";
  sqlite3_stmt *stmt_insert_crs_pvi_checks;
  prepared_statements_map.insert({"sql_insert_crs_pvi_checks",make_tuple(sql_insert_crs_pvi_checks, &stmt_insert_crs_pvi_checks)});

  // ************************* IP FORENSICS **************************
  const char *sql_insert_crs_ip_forensics = "INSERT INTO CRS_61_IP_FORENSICS (UNIQUE_ID, '900036', '900037', '900039') VALUES (:UNIQUE_ID, :900036, :900037, :900039);";
  sqlite3_stmt *stmt_insert_crs_ip_forensics;
  prepared_statements_map.insert({"sql_insert_crs_ip_forensics",make_tuple(sql_insert_crs_ip_forensics, &stmt_insert_crs_ip_forensics)});



  // ************************* IGNORE STATIC **************************
  const char *sql_insert_crs_ignore_static = "INSERT INTO CRS_10_IGNORE_STATIC (UNIQUE_ID, '900040', '900041', '900042', '900043', '999005', '999006') VALUES (:UNIQUE_ID, :900040, :900041, :900042, :900043, :999005, :999006);";
  sqlite3_stmt *stmt_insert_crs_ignore_static;
  prepared_statements_map.insert({"sql_insert_crs_ignore_static",make_tuple(sql_insert_crs_ignore_static, &stmt_insert_crs_ignore_static)});

  // ************************* AV SCANNING **************************
  const char *sql_insert_crs_av_scanning = "INSERT INTO CRS_46_AV_SCANNING (UNIQUE_ID, '981033', '981034', '981035', '950115') VALUES (:UNIQUE_ID, :981033, :981034, :981035, :950115);";
  sqlite3_stmt *stmt_insert_crs_av_scanning;
  prepared_statements_map.insert({"sql_insert_crs_av_scanning",make_tuple(sql_insert_crs_av_scanning, &stmt_insert_crs_av_scanning)});

  // ************************* XML ENABLER **************************
  const char *sql_insert_crs_xml_enabler = "INSERT INTO CRS_13_XML_ENABLER (UNIQUE_ID, '981053') VALUES (:UNIQUE_ID, :981053);";
  sqlite3_stmt *stmt_insert_crs_xml_enabler;
  prepared_statements_map.insert({"sql_insert_crs_xml_enabler",make_tuple(sql_insert_crs_xml_enabler, &stmt_insert_crs_xml_enabler)});

  // ************************* SESSION HIJACKING **************************
  const char *sql_insert_crs_session_hijacking = "INSERT INTO CRS_16_SESSION_HIJACKING (UNIQUE_ID, '981054', '981055', '981056', '981057', '981058', '981059', '981060', '981061', '981062', '981063', '981064') VALUES (:UNIQUE_ID, :981054, :981055, :981056, :981057, :981058, :981059, :981060, :981061, :981062, :981063, :981064);";
  sqlite3_stmt *stmt_insert_crs_session_hijacking;
  prepared_statements_map.insert({"sql_insert_crs_session_hijacking",make_tuple(sql_insert_crs_session_hijacking, &stmt_insert_crs_session_hijacking)});

  // ************************* USERNAME TRACKING **************************
  const char *sql_insert_crs_username_tracking = "INSERT INTO CRS_16_USERNAME_TRACKING (UNIQUE_ID, '981075', '981076', '981077') VALUES (:UNIQUE_ID, :981075, :981076, :981077);";
  sqlite3_stmt *stmt_insert_crs_username_tracking;
  prepared_statements_map.insert({"sql_insert_crs_username_tracking",make_tuple(sql_insert_crs_username_tracking, &stmt_insert_crs_username_tracking)});

  // ************************* CC KNOWN **************************
  const char *sql_insert_crs_cc_known = "INSERT INTO CRS_25_CC_KNOWN (UNIQUE_ID, '981078', '981079', '920005', '920007', '920009', '920011', '920013', '920015', '920017', '981080', '920020', '920006', '920008', '920010', '920012', '920014', '920016', '920018') VALUES (:UNIQUE_ID, :981078, :981079, :920005, :920007, :920009, :920011, :920013, :920015, :920017, :981080, :920020, :920006, :920008, :920010, :920012, :920014, :920016, :920018);";
  sqlite3_stmt *stmt_insert_crs_cc_known;
  prepared_statements_map.insert({"sql_insert_crs_cc_known",make_tuple(sql_insert_crs_cc_known, &stmt_insert_crs_cc_known)});

  // ************************* COMMENT SPAM **************************
  const char *sql_insert_crs_comment_spam = "INSERT INTO CRS_42_COMMENT_SPAM (UNIQUE_ID, '981137', '981138', '981139', '981140', '958297', '999010', '999011', '950923', '950020') VALUES (:UNIQUE_ID, :981137, :981138, :981139, :981140, :958297, :999010, :999011, :950923, :950020);";
  sqlite3_stmt *stmt_insert_crs_comment_spam;
  prepared_statements_map.insert({"sql_insert_crs_comment_spam",make_tuple(sql_insert_crs_comment_spam, &stmt_insert_crs_comment_spam)});

  // ************************* CSRF PROTECTION **************************
  const char *sql_insert_crs_csrf_protection = "INSERT INTO CRS_43_CSRF_PROTECTION (UNIQUE_ID, '981143', '981144', '981145') VALUES (:UNIQUE_ID, :981143, :981144, :981145);";
  sqlite3_stmt *stmt_insert_crs_csrf_protection;
  prepared_statements_map.insert({"sql_insert_crs_csrf_protection",make_tuple(sql_insert_crs_csrf_protection, &stmt_insert_crs_csrf_protection)});

  // ************************* SKIP OUTBOUND CHECKS **************************
  const char *sql_insert_crs_skip_outbound_checks = "INSERT INTO CRS_47_SKIP_OUTBOUND_CHECKS (UNIQUE_ID, '999008') VALUES (:UNIQUE_ID, :999008);";
  sqlite3_stmt *stmt_insert_crs_skip_outbound_checks;
  prepared_statements_map.insert({"sql_insert_crs_skip_outbound_checks",make_tuple(sql_insert_crs_skip_outbound_checks, &stmt_insert_crs_skip_outbound_checks)});

  // ************************* HEADER TAGGING **************************
  const char *sql_insert_crs_header_tagging = "INSERT INTO CRS_49_HEADER_TAGGING (UNIQUE_ID, '900044', '900045') VALUES (:UNIQUE_ID, :900044, :900045);";
  sqlite3_stmt *stmt_insert_crs_header_tagging;
  prepared_statements_map.insert({"sql_insert_crs_header_tagging",make_tuple(sql_insert_crs_header_tagging, &stmt_insert_crs_header_tagging)});

  // ************************* APPLICATION DEFECTS **************************
  const char *sql_insert_crs_application_defects = "INSERT INTO CRS_55_APPLICATION_DEFECTS (UNIQUE_ID, '981219', '981220', '981221', '981222', '981223', '981224', '981238', '981235', '981184', '981236', '981185', '981239', '900046', '981400', '981401', '981402', '981403', '981404', '981405', '981406', '981407', '900048', '981180', '981181', '981182') VALUES (:UNIQUE_ID, :981219, :981220, :981221, :981222, :981223, :981224, :981238, :981235, :981184, :981236, :981185, :981239, :900046, :981400, :981401, :981402, :981403, :981404, :981405, :981406, :981407, :900048, :981180, :981181, :981182);";
  sqlite3_stmt *stmt_insert_crs_application_defects;
  prepared_statements_map.insert({"sql_insert_crs_application_defects",make_tuple(sql_insert_crs_application_defects, &stmt_insert_crs_application_defects)});
  
  // ************************* MARKETING **************************
  const char *sql_insert_crs_marketing = "INSERT INTO CRS_55_MARKETING (UNIQUE_ID, '910008', '910007', '910006') VALUES (:UNIQUE_ID, :910008, :910007, :910006);";
  sqlite3_stmt *stmt_insert_crs_marketing;
  prepared_statements_map.insert({"sql_insert_crs_marketing",make_tuple(sql_insert_crs_marketing, &stmt_insert_crs_marketing)});

  
  
  // WALRUS - sql statement for inserting scores into database
  const char *sql_insert_scores = "INSERT INTO SCORES (UNIQUE_ID, TOTAL_SCORE, CRS_10_SETUP, CRS_20_PROTOCOL_VIOLATIONS, CRS_21_PROTOCOL_ANOMALIES, CRS_23_REQUEST_LIMITS, CRS_30_HTTP_POLICY, CRS_35_BAD_ROBOTS, CRS_40_GENERIC_ATTACKS, CRS_41_SQL_INJECTION_ATTACKS, CRS_41_XSS_ATTACKS, CRS_42_TIGHT_SECURITY, CRS_45_TROJANS, CRS_47_COMMON_EXCEPTIONS, CRS_48_LOCAL_EXCEPTIONS, CRS_49_INBOUND_BLOCKING, CRS_50_OUTBOUND, CRS_59_OUTBOUND_BLOCKING, CRS_60_CORRELATION, CRS_11_BRUTE_FORCE, CRS_11_DOS_PROTECTION, CRS_16_SCANNER_INTEGRATION, CRS_11_PROXY_ABUSE, CRS_11_SLOW_DOS_PROTECTION, CRS_25_CC_TRACK_PAN, CRS_40_APPSENSOR_DETECTION_POINT, CRS_40_HTTP_PARAMETER_POLLUTION, CRS_42_CSP_ENFORCEMENT, CRS_46_SCANNER_INTEGRATION, CRS_48_BAYES_ANALYSIS, CRS_55_RESPONSE_PROFILING, CRS_56_PVI_CHECKS, CRS_61_IP_FORENSICS, CRS_10_IGNORE_STATIC, CRS_11_AVS_TRAFFIC, CRS_13_XML_ENABLER, CRS_16_AUTHENTICATION_TRACKING, CRS_16_SESSION_HIJACKING, CRS_16_USERNAME_TRACKING, CRS_25_CC_KNOWN, CRS_42_COMMENT_SPAM, CRS_43_CSRF_PROTECTION, CRS_46_AV_SCANNING, CRS_47_SKIP_OUTBOUND_CHECKS, CRS_49_HEADER_TAGGING, CRS_55_APPLICATION_DEFECTS, CRS_55_MARKETING, CRS_59_HEADER_TAGGING) VALUES (:UNIQUE_ID, :total_score, :crs_10_setup, :crs_20_protocol_violations, :crs_21_protocol_anomalies, :crs_23_request_limits, :crs_30_http_policy, :crs_35_bad_robots, :crs_40_generic_attacks, :crs_41_sql_injection_attacks, :crs_41_xss_attacks, :crs_42_tight_security, :crs_45_trojans, :crs_47_common_exceptions, :crs_48_local_exceptions, :crs_49_inbound_blocking, :crs_50_outbound, :crs_59_outbound_blocking, :crs_60_correlation, :crs_11_brute_force, :crs_11_dos_protection, :crs_16_scanner_integration, :crs_11_proxy_abuse, :crs_11_slow_dos_protection, :crs_25_cc_track_pan, :crs_40_appsensor_detection_point, :crs_40_http_parameter_pollution, :crs_42_csp_enforcement, :crs_46_scanner_integration, :crs_48_bayes_analysis, :crs_55_response_profiling, :crs_56_pvi_checks, :crs_61_ip_forensics, :crs_10_ignore_static, :crs_11_avs_traffic, :crs_13_xml_enabler, :crs_16_authentication_tracking, :crs_16_session_hijacking, :crs_16_username_tracking, :crs_25_cc_known, :crs_42_comment_spam, :crs_43_csrf_protection, :crs_46_av_scanning, :crs_47_skip_outbound_checks, :crs_49_header_tagging, :crs_55_application_defects, :crs_55_marketing, :crs_59_header_tagging);";
  sqlite3_stmt *stmt_insert_scores;
  prepared_statements_map.insert({"sql_insert_scores",make_tuple(sql_insert_scores, &stmt_insert_scores)});
  
  
  
  
  //************************************************************************************************
  
  // start a transaction - all of the statements from here until END TRANSACTION will be queued and executed at once,
  // reducing the overhead associated with committing to the database multiple times (massive speed improvement)
  sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

  
  
  
  
    
  
  // variables for sql compilation
  const char *pzTail; // pointer to uncompiled portion of statement
  
  int prepared_statement_errors = 0; // sql compilation error counter
  for (const auto &s : prepared_statements_map) {
    rc = sqlite3_prepare_v2(db, get<0>(s.second), strlen(get<0>(s.second)), get<1>(s.second), &pzTail);
    if( rc != SQLITE_OK ){
      cerr << "SQL error compiling " << s.first << " prepared statement" << endl;
      cerr << "The error was: "<< sqlite3_errmsg(db) << endl;
      ++prepared_statement_errors;
    } else {
      if (debug) {cout << "Prepared statement " << s.first << " was compiled successfully" << endl;}
    }
  }
  

  
  
  // integers for rule ID counting WALRUS not required any more but can't delete them until ruleIDmap structure is changed
  int CRS_SEPARATE_RULES_MATCHED, CRS_PROTOCOL_VIOLATION, CRS_PROTOCOL_ANOMALY, CRS_REQUEST_LIMIT, CRS_HTTP_POLICY, CRS_BAD_ROBOT, CRS_GENERIC_ATTACK, CRS_SQL_INJECTION, CRS_XSS_ATTACK, CRS_TIGHT_SECURITY, CRS_TROJANS, CRS_COMMON_EXCEPTIONS, CRS_LOCAL_EXCEPTIONS, CRS_INBOUND_BLOCKING, CRS_OUTBOUND, CRS_OUTBOUND_BLOCKING, CRS_CORRELATION, CRS_BRUTE_FORCE, CRS_DOS, CRS_PROXY_ABUSE, CRS_SLOW_DOS, CRS_CC_TRACK_PAN, CRS_APPSENSOR, CRS_HTTP_PARAMETER_POLLUTION, CRS_CSP_ENFORCEMENT, CRS_SCANNER_INTEGRATION, CRS_BAYES_ANALYSIS, CRS_RESPONSE_PROFILING, CRS_PVI_CHECKS, CRS_IP_FORENSICS, CRS_IGNORE_STATIC, CRS_AVS_TRAFFIC, CRS_XML_ENABLER, CRS_AUTHENTICATION_TRACKING, CRS_SESSION_HIJACKING, CRS_USERNAME_TRACKING, CRS_CC_KNOWN, CRS_COMMENT_SPAM, CRS_CSRF_PROTECTION, CRS_AV_SCANNING, CRS_SKIP_OUTBOUND_CHECKS, CRS_HEADER_TAGGING, CRS_APPLICATION_DEFECTS, CRS_MARKETING;
  
  
  
  
  
  
  
  
  // WALRUS - change this map, group counters (second part of tuple) not required any more
  map <string, tuple<sqlite3_stmt **,int *>> ruleIDmap = {	{"960911",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"981227",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960000",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960912",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960914",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960915",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960016",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960011",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960012",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960902",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960022",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960020",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"958291",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"958230",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"958231",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"958295",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"950107",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"950109",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"950108",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"950801",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"950116",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960014",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960901",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960018",	make_tuple(&stmt_insert_crs_protocol_violation,		&CRS_PROTOCOL_VIOLATION		)	},
								{"960008",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960007",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960015",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960021",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960009",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960006",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960904",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960017",	make_tuple(&stmt_insert_crs_protocol_anomaly,		&CRS_PROTOCOL_ANOMALY		)	},
								{"960209",	make_tuple(&stmt_insert_crs_request_limit,		&CRS_REQUEST_LIMIT		)	},
								{"960208",	make_tuple(&stmt_insert_crs_request_limit,		&CRS_REQUEST_LIMIT		)	},
								{"960335",	make_tuple(&stmt_insert_crs_request_limit,		&CRS_REQUEST_LIMIT		)	},
								{"960341",	make_tuple(&stmt_insert_crs_request_limit,		&CRS_REQUEST_LIMIT		)	},
								{"960342",	make_tuple(&stmt_insert_crs_request_limit,		&CRS_REQUEST_LIMIT		)	},
								{"960343",	make_tuple(&stmt_insert_crs_request_limit,		&CRS_REQUEST_LIMIT		)	},
								{"960032",	make_tuple(&stmt_insert_crs_http_policy,		&CRS_HTTP_POLICY		)	},
								{"960010",	make_tuple(&stmt_insert_crs_http_policy,		&CRS_HTTP_POLICY		)	},
								{"960034",	make_tuple(&stmt_insert_crs_http_policy,		&CRS_HTTP_POLICY		)	},
								{"960035",	make_tuple(&stmt_insert_crs_http_policy,		&CRS_HTTP_POLICY		)	},
								{"960038",	make_tuple(&stmt_insert_crs_http_policy,		&CRS_HTTP_POLICY		)	},
								{"990002",	make_tuple(&stmt_insert_crs_bad_robot,			&CRS_BAD_ROBOT			)	},
								{"990901",	make_tuple(&stmt_insert_crs_bad_robot,			&CRS_BAD_ROBOT			)	},
								{"990902",	make_tuple(&stmt_insert_crs_bad_robot,			&CRS_BAD_ROBOT			)	},
								{"990012",	make_tuple(&stmt_insert_crs_bad_robot,			&CRS_BAD_ROBOT			)	},
								{"950907",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"960024",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950008",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950010",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950011",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950018",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950019",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950012",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950910",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950911",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950117",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950118",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950119",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950120",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"981133",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950009",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950003",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950000",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950005",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950002",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"950006",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"959151",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"958976",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"958977",	make_tuple(&stmt_insert_crs_generic_attack,		&CRS_GENERIC_ATTACK		)	},
								{"981231",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981260",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981318",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981319",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"950901",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981320",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981300",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981301",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981302",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981303",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981304",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981305",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981306",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981307",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981308",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981309",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981310",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981311",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981312",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981313",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981314",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981315",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981316",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981317",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"950007",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"950001",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"959070",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"959071",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"959072",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"950908",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"959073",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981172",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981173",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981272",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981244",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981255",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981257",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981248",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981277",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981250",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981241",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981252",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981256",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981245",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981276",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981254",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981270",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981240",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981249",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981253",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981242",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981246",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981251",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981247",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"981243",	make_tuple(&stmt_insert_crs_sql_injection,		&CRS_SQL_INJECTION		)	},
								{"973336",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973337",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973338",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"981136",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"981018",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958016",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958414",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958032",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958026",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958027",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958054",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958418",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958034",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958019",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958013",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958408",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958012",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958423",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958002",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958017",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958007",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958047",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958410",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958415",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958022",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958405",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958419",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958028",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958057",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958031",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958006",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958033",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958038",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958409",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958001",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958005",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958404",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958023",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958010",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958411",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958422",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958036",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958000",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958018",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958406",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958040",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958052",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958037",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958049",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958030",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958041",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958416",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958024",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958059",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958417",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958020",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958045",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958004",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958421",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958009",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958025",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958413",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958051",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958420",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958407",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958056",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958011",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958412",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958008",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958046",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958039",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"958003",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973300",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973301",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973302",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973303",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973304",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973305",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973306",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973307",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973308",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973309",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973310",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973311",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973312",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973313",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973314",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973331",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973315",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973330",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973327",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973326",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973346",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973345",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973324",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973323",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973322",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973348",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973321",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973320",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973318",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973317",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973347",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973335",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973334",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973333",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973344",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973332",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973329",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973328",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973316",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973325",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"973319",	make_tuple(&stmt_insert_crs_xss_attack,			&CRS_XSS_ATTACK			)	},
								{"950103",	make_tuple(&stmt_insert_crs_tight_security,		&CRS_TIGHT_SECURITY		)	},
								{"950110",	make_tuple(&stmt_insert_crs_trojans,			&CRS_TROJANS			)	},
								{"950921",	make_tuple(&stmt_insert_crs_trojans,			&CRS_TROJANS			)	},
								{"950922",	make_tuple(&stmt_insert_crs_trojans,			&CRS_TROJANS			)	},
								{"981020",	make_tuple(&stmt_insert_crs_common_exceptions,		&CRS_COMMON_EXCEPTIONS		)	},
								{"981021",	make_tuple(&stmt_insert_crs_common_exceptions,		&CRS_COMMON_EXCEPTIONS		)	},
								{"981022",	make_tuple(&stmt_insert_crs_common_exceptions,		&CRS_COMMON_EXCEPTIONS		)	},
								{"981175",	make_tuple(&stmt_insert_crs_inbound_blocking,		&CRS_INBOUND_BLOCKING		)	},
								{"981176",	make_tuple(&stmt_insert_crs_inbound_blocking,		&CRS_INBOUND_BLOCKING		)	},
								{"970007",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970008",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970009",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970010",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970012",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970903",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970016",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970018",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970901",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970021",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970011",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981177",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981000",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981001",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981003",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981004",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981005",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981006",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981007",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981178",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970014",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970015",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970902",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970002",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970003",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970004",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970904",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"970013",	make_tuple(&stmt_insert_crs_outbound,			&CRS_OUTBOUND			)	},
								{"981200",	make_tuple(&stmt_insert_crs_outbound_blocking,		&CRS_OUTBOUND_BLOCKING		)	},
								{"981201",	make_tuple(&stmt_insert_crs_correlation,		&CRS_CORRELATION		)	},
								{"981202",	make_tuple(&stmt_insert_crs_correlation,		&CRS_CORRELATION		)	},
								{"981203",	make_tuple(&stmt_insert_crs_correlation,		&CRS_CORRELATION		)	},
								{"981204",	make_tuple(&stmt_insert_crs_correlation,		&CRS_CORRELATION		)	},
								{"981205",	make_tuple(&stmt_insert_crs_correlation,		&CRS_CORRELATION		)	},
								{"981036",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981037",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981038",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981039",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981040",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981041",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981042",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981043",	make_tuple(&stmt_insert_crs_brute_force,		&CRS_BRUTE_FORCE		)	},
								{"981044",	make_tuple(&stmt_insert_crs_dos,			&CRS_DOS			)	},
								{"981045",	make_tuple(&stmt_insert_crs_dos,			&CRS_DOS			)	},
								{"981046",	make_tuple(&stmt_insert_crs_dos,			&CRS_DOS			)	},
								{"981047",	make_tuple(&stmt_insert_crs_dos,			&CRS_DOS			)	},
								{"981048",	make_tuple(&stmt_insert_crs_dos,			&CRS_DOS			)	},
								{"981049",	make_tuple(&stmt_insert_crs_dos,			&CRS_DOS			)	},
								{"981050",	make_tuple(&stmt_insert_crs_proxy_abuse,		&CRS_PROXY_ABUSE		)	},
								{"981051",	make_tuple(&stmt_insert_crs_slow_dos,			&CRS_SLOW_DOS			)	},
								{"981052",	make_tuple(&stmt_insert_crs_slow_dos,			&CRS_SLOW_DOS			)	},
								{"900030",	make_tuple(&stmt_insert_crs_scanner_integration,	&CRS_SCANNER_INTEGRATION	)	},
								{"900031",	make_tuple(&stmt_insert_crs_scanner_integration,	&CRS_SCANNER_INTEGRATION	)	},
								{"920021",	make_tuple(&stmt_insert_crs_cc_track_pan,		&CRS_CC_TRACK_PAN		)	},
								{"920022",	make_tuple(&stmt_insert_crs_cc_track_pan,		&CRS_CC_TRACK_PAN		)	},
								{"920023",	make_tuple(&stmt_insert_crs_cc_track_pan,		&CRS_CC_TRACK_PAN		)	},
								{"981082",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981083",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981084",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981085",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981086",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981087",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981088",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981089",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981090",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981091",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981092",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981093",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981094",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981095",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981096",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981097",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981103",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981104",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981110",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981105",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981098",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981099",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981100",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981101",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981102",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981131",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"981132",	make_tuple(&stmt_insert_crs_appsensor,			&CRS_APPSENSOR			)	},
								{"900032",	make_tuple(&stmt_insert_crs_http_parameter_pollution,	&CRS_HTTP_PARAMETER_POLLUTION	)	},
								{"981142",	make_tuple(&stmt_insert_crs_csp_enforcement,		&CRS_CSP_ENFORCEMENT		)	},
								{"960001",	make_tuple(&stmt_insert_crs_csp_enforcement,		&CRS_CSP_ENFORCEMENT		)	},
								{"960002",	make_tuple(&stmt_insert_crs_csp_enforcement,		&CRS_CSP_ENFORCEMENT		)	},
								{"960003",	make_tuple(&stmt_insert_crs_csp_enforcement,		&CRS_CSP_ENFORCEMENT		)	},
								{"999003",	make_tuple(&stmt_insert_crs_scanner_integration,	&CRS_SCANNER_INTEGRATION	)	},
								{"999004",	make_tuple(&stmt_insert_crs_scanner_integration,	&CRS_SCANNER_INTEGRATION	)	},
								{"900033",	make_tuple(&stmt_insert_crs_bayes_analysis,		&CRS_BAYES_ANALYSIS		)	},
								{"900034",	make_tuple(&stmt_insert_crs_bayes_analysis,		&CRS_BAYES_ANALYSIS		)	},
								{"900035",	make_tuple(&stmt_insert_crs_bayes_analysis,		&CRS_BAYES_ANALYSIS		)	},
								{"981187",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981189",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981190",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981191",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981192",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981193",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981194",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981195",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981196",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981197",	make_tuple(&stmt_insert_crs_response_profiling,		&CRS_RESPONSE_PROFILING		)	},
								{"981198",	make_tuple(&stmt_insert_crs_pvi_checks,			&CRS_PVI_CHECKS			)	},
								{"981199",	make_tuple(&stmt_insert_crs_pvi_checks,			&CRS_PVI_CHECKS			)	},
								{"900036",	make_tuple(&stmt_insert_crs_ip_forensics,		&CRS_IP_FORENSICS		)	},
								{"900037",	make_tuple(&stmt_insert_crs_ip_forensics,		&CRS_IP_FORENSICS		)	},
								{"900039",	make_tuple(&stmt_insert_crs_ip_forensics,		&CRS_IP_FORENSICS		)	},
								{"900040",	make_tuple(&stmt_insert_crs_ignore_static,		&CRS_IGNORE_STATIC		)	},
								{"900041",	make_tuple(&stmt_insert_crs_ignore_static,		&CRS_IGNORE_STATIC		)	},
								{"900042",	make_tuple(&stmt_insert_crs_ignore_static,		&CRS_IGNORE_STATIC		)	},
								{"900043",	make_tuple(&stmt_insert_crs_ignore_static,		&CRS_IGNORE_STATIC		)	},
								{"999005",	make_tuple(&stmt_insert_crs_ignore_static,		&CRS_IGNORE_STATIC		)	},
								{"999006",	make_tuple(&stmt_insert_crs_ignore_static,		&CRS_IGNORE_STATIC		)	},
								{"981033",	make_tuple(&stmt_insert_crs_av_scanning,		&CRS_AV_SCANNING		)	},
								{"981034",	make_tuple(&stmt_insert_crs_av_scanning,		&CRS_AV_SCANNING		)	},
								{"981035",	make_tuple(&stmt_insert_crs_av_scanning,		&CRS_AV_SCANNING		)	},
								{"981053",	make_tuple(&stmt_insert_crs_xml_enabler,		&CRS_XML_ENABLER		)	},
								{"981054",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981055",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981056",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981057",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981058",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981059",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981060",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981061",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981062",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981063",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981064",	make_tuple(&stmt_insert_crs_session_hijacking,		&CRS_SESSION_HIJACKING		)	},
								{"981075",	make_tuple(&stmt_insert_crs_username_tracking,		&CRS_USERNAME_TRACKING		)	},
								{"981076",	make_tuple(&stmt_insert_crs_username_tracking,		&CRS_USERNAME_TRACKING		)	},
								{"981077",	make_tuple(&stmt_insert_crs_username_tracking,		&CRS_USERNAME_TRACKING		)	},
								{"981078",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"981079",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920005",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920007",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920009",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920011",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920013",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920015",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920017",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"981080",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920020",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920006",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920008",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920010",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920012",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920014",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920016",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"920018",	make_tuple(&stmt_insert_crs_cc_known,			&CRS_CC_KNOWN			)	},
								{"981137",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"981138",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"981139",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"981140",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"958297",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"999010",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"999011",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"950923",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"950020",	make_tuple(&stmt_insert_crs_comment_spam,		&CRS_COMMENT_SPAM		)	},
								{"981143",	make_tuple(&stmt_insert_crs_csrf_protection,		&CRS_CSRF_PROTECTION		)	},
								{"981144",	make_tuple(&stmt_insert_crs_csrf_protection,		&CRS_CSRF_PROTECTION		)	},
								{"981145",	make_tuple(&stmt_insert_crs_csrf_protection,		&CRS_CSRF_PROTECTION		)	},
								{"950115",	make_tuple(&stmt_insert_crs_av_scanning,		&CRS_AV_SCANNING		)	},
								{"999008",	make_tuple(&stmt_insert_crs_skip_outbound_checks,	&CRS_SKIP_OUTBOUND_CHECKS	)	},
								{"900044",	make_tuple(&stmt_insert_crs_header_tagging,		&CRS_HEADER_TAGGING		)	},
								{"900045",	make_tuple(&stmt_insert_crs_header_tagging,		&CRS_HEADER_TAGGING		)	},
								{"981219",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981220",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981221",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981222",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981223",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981224",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981238",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981235",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981184",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981236",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981185",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981239",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"900046",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981400",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981401",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981402",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981403",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981404",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981405",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981406",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981407",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"900048",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981180",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981181",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"981182",	make_tuple(&stmt_insert_crs_application_defects,	&CRS_APPLICATION_DEFECTS	)	},
								{"910008",	make_tuple(&stmt_insert_crs_marketing,			&CRS_MARKETING			)	},
								{"910007",	make_tuple(&stmt_insert_crs_marketing,			&CRS_MARKETING			)	},
								{"910006",	make_tuple(&stmt_insert_crs_marketing,			&CRS_MARKETING			)	} };

     
  
  
  if (prepared_statement_errors != 0) {
    cerr << "Skipping logfile processing due to failed prepared statement creation" << endl;
  } else {
    
    // create stream for reading logfile
    ifstream in(logfile);
    int line = 0;
    string linedata;
  

    
    // initialise strings for each value to be bound to the sqlite statement
    string UNIQUE_ID, HEADER, A, B, C, D, E, F, G, H, I, J, K; // "high level" strings
    
    // strings for matches in A
    string TIMESTAMP, UNIXTIME, SOURCE_IP, SOURCE_PORT, DESTINATION_IP, DESTINATION_PORT;
    int SOURCE_IP_ID, SOURCE_PORT_ID, DESTINATION_IP_ID, DESTINATION_PORT_ID;
    
    // strings for matches in B
    string REQUEST_METHOD, REQUEST_URI, REQUEST_HTTP_VERSION; // first regex
    string REQUEST_HOST, REQUEST_CONNECTION, REQUEST_ACCEPT, REQUEST_USER_AGENT, REQUEST_DNT, REQUEST_REFERRER, REQUEST_ACCEPT_ENCODING, REQUEST_ACCEPT_LANGUAGE, REQUEST_COOKIE, REQUEST_X_REQUESTED_WITH, REQUEST_CONTENT_TYPE, REQUEST_CONTENT_LENGTH, REQUEST_PROXY_CONNECTION, REQUEST_ACCEPT_CHARSET, REQUEST_UA_CPU, REQUEST_X_FORWARDED_FOR, REQUEST_CACHE_CONTROL, REQUEST_VIA, REQUEST_IF_MODIFIED_SINCE, REQUEST_IF_NONE_MATCH, REQUEST_PRAGMA;
    int REQUEST_METHOD_ID, REQUEST_URI_ID, REQUEST_HTTP_VERSION_ID;
    int REQUEST_HOST_ID, REQUEST_CONNECTION_ID, REQUEST_ACCEPT_ID, REQUEST_USER_AGENT_ID, REQUEST_DNT_ID, REQUEST_REFERRER_ID, REQUEST_ACCEPT_ENCODING_ID, REQUEST_ACCEPT_LANGUAGE_ID, REQUEST_COOKIE_ID, REQUEST_X_REQUESTED_WITH_ID, REQUEST_CONTENT_TYPE_ID, REQUEST_CONTENT_LENGTH_ID, REQUEST_PROXY_CONNECTION_ID, REQUEST_ACCEPT_CHARSET_ID, REQUEST_UA_CPU_ID, REQUEST_X_FORWARDED_FOR_ID, REQUEST_CACHE_CONTROL_ID, REQUEST_VIA_ID, REQUEST_IF_MODIFIED_SINCE_ID, REQUEST_IF_NONE_MATCH_ID, REQUEST_PRAGMA_ID;
    
    
    // strings for matches in F
    string RESPONSE_HTTP_VERSION, RESPONSE_HTTP_STATUS_CODE, RESPONSE_HTTP_STATUS_TEXT, RESPONSE_X_POWERED_BY, RESPONSE_EXPIRES, RESPONSE_CACHE_CONTROL, RESPONSE_PRAGMA, RESPONSE_VARY, RESPONSE_CONTENT_ENCODING, RESPONSE_CONTENT_LENGTH, RESPONSE_CONNECTION, RESPONSE_CONTENT_TYPE, RESPONSE_STATUS, RESPONSE_KEEP_ALIVE;
    int RESPONSE_HTTP_VERSION_ID, RESPONSE_HTTP_STATUS_CODE_ID, RESPONSE_HTTP_STATUS_TEXT_ID, RESPONSE_X_POWERED_BY_ID, RESPONSE_EXPIRES_ID, RESPONSE_CACHE_CONTROL_ID, RESPONSE_PRAGMA_ID, RESPONSE_VARY_ID, RESPONSE_CONTENT_ENCODING_ID, RESPONSE_CONTENT_LENGTH_ID, RESPONSE_CONNECTION_ID, RESPONSE_CONTENT_TYPE_ID, RESPONSE_STATUS_ID, RESPONSE_KEEP_ALIVE_ID;
    
    
    // strings for matches in H
    string TRAILER_MESSAGES, TRAILER_APACHE_HANDLER, TRAILER_APACHE_ERROR, TRAILER_STOPWATCH, TRAILER_STOPWATCH2, TRAILER_PRODUCER, TRAILER_SERVER, TRAILER_ENGINE_MODE, TRAILER_ACTION, TRAILER_XML_PARSER_ERROR;
    int TRAILER_MESSAGES_ID, TRAILER_APACHE_HANDLER_ID, TRAILER_APACHE_ERROR_ID, TRAILER_STOPWATCH_ID, TRAILER_STOPWATCH2_ID, TRAILER_PRODUCER_ID, TRAILER_SERVER_ID, TRAILER_ENGINE_MODE_ID, TRAILER_ACTION_ID, TRAILER_XML_PARSER_ERROR_ID;
    
    
    
    
    // 3. start on vector row 1. determine the header letter type
    // stop at penultimate row or we won't be able to find the last line number    
    for ( int r = 0; r < rows -1; ++r) {
      // header letter is always the 12th character in the string (11th index 0)
      char letter = results[r].second[11];
      int startline = results[r].first;
      int endline = results[r+1].first;
      
      if (debug) {cout << "Row " << r << " - letter is: " << letter << " - start line is: " << startline << " - end line is: " << endline << endl;}
      
      // initialise a string to hold the whole of the header data, will be re-created for each pass of the for loop
      string headerdata;
      
      // each time this is called it seems to start from where it left off before
      while (getline(in, linedata)) {
	++line;
	// if the data is in between two headers, append it to the headerdata string
	if (line > startline && line < endline ) {
	  if (debug) {cout << "Appending line data on line: " << line << endl;}
	  headerdata.append(linedata);
	  headerdata.append(string("\n"));
	  
	} else if (line == endline) {
	  if (debug) {
	    cout << "Reached endline, current string is:" << endl;
	    cout << headerdata << endl;
	  }
	    
	  // store the headerdata in the appropriate position in the array
	  if (letter == 'A') {
	    if (debug) {cout << "Letter is A" << endl;}
	    HEADER=results[r].second;
	    A = headerdata;
	    // submatch the apache UNIQUE_ID from the A header
	    if (boost::regex_match(A.c_str(), match, A_regex)) {
	      // WALRUS
              TIMESTAMP = match[1]; // something like 14/Jun/2015:09:32:25 +0100
              // need to convert this timestamp to a sqlite timestamp YYYY-MM-DD HH:MM:SS[+-]HH:MM
              // try this http://www.thejach.com/view/2012/7/apaches_common_log_format_datetime_converted_to_unix_timestamp_with_c
              // then use sqlite's internal mechanism to convert from unix timestamp to something more user friendly
              UNIXTIME=logtimeToUnix(TIMESTAMP);
              if (debug) {cout << "Apache timestamp is " << TIMESTAMP << " Unix timestamp is " << UNIXTIME << endl;}
              
              
              
              
	      UNIQUE_ID = match[2];
              
	      SOURCE_IP = match[3];
              SOURCE_PORT = match[4];
	      DESTINATION_IP = match[5];
	      DESTINATION_PORT = match[6];
	      if(debug) {cout << "Apache UNIQUE_ID for header " << line << " is: " << UNIQUE_ID << endl;}
	    } else {
	      cerr << "No Apache Unique ID found" << endl;
	    }
	    
	    // get integer IDs from the map
            SOURCE_IP_ID = ID_from_map(SOURCE_IP,source_ip_map,debug);
            SOURCE_PORT_ID = ID_from_map(SOURCE_PORT,source_port_map,debug);
            DESTINATION_IP_ID = ID_from_map(DESTINATION_IP,destination_ip_map,debug);
            DESTINATION_PORT_ID = ID_from_map(DESTINATION_PORT,destination_port_map,debug);

	    // UNIQUE_ID must be bound to all statements
	    if (debug) {cout << "Binding unique ID to statements" << endl;};
	    for (const auto &s : prepared_statements_map) {
	      int rc_bind = sqlite3_bind_text(*(get<1>(s.second)), sqlite3_bind_parameter_index(*(get<1>(s.second)), ":UNIQUE_ID"), UNIQUE_ID.c_str(), UNIQUE_ID.length(), 0);
	      if (rc_bind != SQLITE_OK) {
		cerr << UNIQUE_ID << ": error binding unique ID to statement " << s.first << ". Code " << rc_bind << " description: " << sqlite3_errmsg(db) << endl;
	      } else {
		if (debug) {cout << UNIQUE_ID << ": unique ID bound to " << s.first << " successfully" << endl;}
	      }
	    }
	    
	    
	    
	    // header and A data bound to insert_main sql statement
	    if (debug) {cout << "Binding data from A to table main prepared statement" << endl;};
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":HEADER"), HEADER.c_str(), HEADER.length(), 0);
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":A"), A.c_str(), A.length(), 0);
	    
	    // these values bound to insert_A sql statement
	    if (debug) {cout << "Binding data for table A" << endl;};
	    sqlite3_bind_text(stmt_insert_A, sqlite3_bind_parameter_index(stmt_insert_A, ":TIMESTAMP"), TIMESTAMP.c_str(), TIMESTAMP.length(), 0);
	    sqlite3_bind_text(stmt_insert_A, sqlite3_bind_parameter_index(stmt_insert_A, ":UNIXTIME"), UNIXTIME.c_str(), UNIXTIME.length(), 0);
            
            
            // bind ID integers
            bind_ID (stmt_insert_A, ":SOURCE_IP_ID", SOURCE_IP_ID, debug);
            bind_ID (stmt_insert_A, ":SOURCE_PORT_ID", SOURCE_PORT_ID, debug);
            bind_ID (stmt_insert_A, ":DESTINATION_IP_ID", DESTINATION_IP_ID, debug);
            bind_ID (stmt_insert_A, ":DESTINATION_PORT_ID", DESTINATION_PORT_ID, debug);



	    
	    
	    
	  } else if (letter == 'B') {
	    if (debug) {cout << "Letter is B" << endl;}
	    B = headerdata;
	    // submatch some relevant bits from B
	    if (boost::regex_match(B.c_str(), match, B_regex)) {
	      REQUEST_METHOD = match[1];
	      REQUEST_URI = match[2];
	      REQUEST_HTTP_VERSION = match[3];
              
	    } else {
	      cerr << "Regex matching at B failed" << endl;
	    }
	    // get integer IDs from the map
            REQUEST_METHOD_ID = ID_from_map(REQUEST_METHOD,request_method_map,debug);
            REQUEST_URI_ID = ID_from_map(REQUEST_URI,uri_map,debug);
            REQUEST_HTTP_VERSION_ID = ID_from_map(REQUEST_HTTP_VERSION,http_version_b_map,debug);
	    
	    if (boost::regex_search(B.c_str(), match, B_regex_host)) {
	      REQUEST_HOST = match[1];
	    }
	    REQUEST_HOST_ID = ID_from_map(REQUEST_HOST,hosts_map,debug);
	    
	    if (boost::regex_search(B.c_str(), match, B_regex_connection)) {
	      REQUEST_CONNECTION = match[1];
	    }
	    REQUEST_CONNECTION_ID = ID_from_map(REQUEST_CONNECTION,connection_b_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_accept)) {
	      REQUEST_ACCEPT = match[1];
	    }
	    REQUEST_ACCEPT_ID = ID_from_map(REQUEST_ACCEPT,accept_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_useragent)) {
	      REQUEST_USER_AGENT = match[1];
	    }
	    REQUEST_USER_AGENT_ID = ID_from_map(REQUEST_USER_AGENT,user_agent_map,debug);
	    
	    if (boost::regex_search(B.c_str(), match, B_regex_DNT)) {
	      REQUEST_DNT = match[1];
	    }
	    REQUEST_DNT_ID = ID_from_map(REQUEST_DNT,dnt_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_referrer)) {
	      REQUEST_REFERRER = match[1];
	    }
	    REQUEST_REFERRER_ID = ID_from_map(REQUEST_REFERRER,referrer_map,debug);
	    
	    if (boost::regex_search(B.c_str(), match, B_regex_accept_encoding)) {
	      REQUEST_ACCEPT_ENCODING = match[1];
	    }
	    REQUEST_ACCEPT_ENCODING_ID = ID_from_map(REQUEST_ACCEPT_ENCODING,accept_encoding_map,debug);
	    
	    if (boost::regex_search(B.c_str(), match, B_regex_accept_language)) {
	      REQUEST_ACCEPT_LANGUAGE = match[1];
	    }
	    REQUEST_ACCEPT_LANGUAGE_ID = ID_from_map(REQUEST_ACCEPT_LANGUAGE,accept_language_map,debug);
	    
	    if (boost::regex_search(B.c_str(), match, B_regex_cookie)) {
	      REQUEST_COOKIE = match[1];
	    }
	    REQUEST_COOKIE_ID = ID_from_map(REQUEST_COOKIE,cookie_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_x_requested_with)) {
	      REQUEST_X_REQUESTED_WITH = match[1];
	    }
	    REQUEST_X_REQUESTED_WITH_ID = ID_from_map(REQUEST_X_REQUESTED_WITH,x_requested_with_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_content_type)) {
	      REQUEST_CONTENT_TYPE = match[1];
	    }
	    REQUEST_CONTENT_TYPE_ID = ID_from_map(REQUEST_CONTENT_TYPE,content_type_b_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_content_length)) {
	      REQUEST_CONTENT_LENGTH = match[1];
	    }
	    REQUEST_CONTENT_LENGTH_ID = ID_from_map(REQUEST_CONTENT_LENGTH,content_length_b_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_proxy_connection)) {
	      REQUEST_PROXY_CONNECTION = match[1];
	    }
	    REQUEST_PROXY_CONNECTION_ID = ID_from_map(REQUEST_PROXY_CONNECTION,proxy_connection_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_accept_charset)) {
	      REQUEST_ACCEPT_CHARSET = match[1];
	    }
	    REQUEST_ACCEPT_CHARSET_ID = ID_from_map(REQUEST_ACCEPT_CHARSET,accept_charset_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_UA_CPU)) {
	      REQUEST_UA_CPU = match[1];
	    }
	    REQUEST_UA_CPU_ID = ID_from_map(REQUEST_UA_CPU,ua_cpu_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_x_forwarded_for)) {
	      REQUEST_X_FORWARDED_FOR = match[1];
	    }
	    REQUEST_X_FORWARDED_FOR_ID = ID_from_map(REQUEST_X_FORWARDED_FOR,x_forwarded_for_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_cache_control)) {
	      REQUEST_CACHE_CONTROL = match[1];
	    }
	    REQUEST_CACHE_CONTROL_ID = ID_from_map(REQUEST_CACHE_CONTROL,cache_control_b_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_via)) {
	      REQUEST_VIA = match[1];
	    }
	    REQUEST_VIA_ID = ID_from_map(REQUEST_VIA,via_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_if_modified_since)) {
	      REQUEST_IF_MODIFIED_SINCE = match[1];
	    }
	    REQUEST_IF_MODIFIED_SINCE_ID = ID_from_map(REQUEST_IF_MODIFIED_SINCE,if_modified_since_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_if_none_match)) {
	      REQUEST_IF_NONE_MATCH = match[1];
	    }
	    REQUEST_IF_NONE_MATCH_ID = ID_from_map(REQUEST_IF_NONE_MATCH,if_none_match_map,debug);
            
	    if (boost::regex_search(B.c_str(), match, B_regex_pragma)) {
	      REQUEST_PRAGMA = match[1];
	    }
	    REQUEST_PRAGMA_ID = ID_from_map(REQUEST_PRAGMA,pragma_b_map,debug);
	    
	    // bind whole B string
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":B"), B.c_str(), B.length(), 0);
	    
            // bind the ID integers
            bind_ID (stmt_insert_B, ":REQUEST_METHOD_ID", REQUEST_METHOD_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_URI_ID", REQUEST_URI_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_HTTP_VERSION_ID", REQUEST_HTTP_VERSION_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_HOST_ID", REQUEST_HOST_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_USER_AGENT_ID", REQUEST_USER_AGENT_ID, debug);
	    bind_ID (stmt_insert_B, ":REQUEST_CONNECTION_ID", REQUEST_CONNECTION_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_ACCEPT_ID", REQUEST_ACCEPT_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_DNT_ID", REQUEST_DNT_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_REFERRER_ID", REQUEST_REFERRER_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_ACCEPT_ENCODING_ID", REQUEST_ACCEPT_ENCODING_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_ACCEPT_LANGUAGE_ID", REQUEST_ACCEPT_LANGUAGE_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_COOKIE_ID", REQUEST_COOKIE_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_X_REQUESTED_WITH_ID", REQUEST_X_REQUESTED_WITH_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_CONTENT_TYPE_ID", REQUEST_CONTENT_TYPE_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_CONTENT_LENGTH_ID", REQUEST_CONTENT_LENGTH_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_PROXY_CONNECTION_ID", REQUEST_PROXY_CONNECTION_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_ACCEPT_CHARSET_ID", REQUEST_ACCEPT_CHARSET_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_UA_CPU_ID", REQUEST_UA_CPU_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_X_FORWARDED_FOR_ID", REQUEST_X_FORWARDED_FOR_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_CACHE_CONTROL_ID", REQUEST_CACHE_CONTROL_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_VIA_ID", REQUEST_VIA_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_IF_MODIFIED_SINCE_ID", REQUEST_IF_MODIFIED_SINCE_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_IF_NONE_MATCH_ID", REQUEST_IF_NONE_MATCH_ID, debug);
            bind_ID (stmt_insert_B, ":REQUEST_PRAGMA_ID", REQUEST_PRAGMA_ID, debug);
	    

	    
	  } else if (letter == 'C') {
	    if (debug) {cout << "Letter is C" << endl;}
	    C = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":C"), C.c_str(), C.length(), 0);	    

	    
	    
	  } else if (letter == 'D') {
	    if (debug) {cout << "Letter is D" << endl;}
	    D = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":D"), D.c_str(), D.length(), 0);	    

	    
	    
	  } else if (letter == 'E') {
	    if (debug) {cout << "Letter is E" << endl;}
	    E = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":E"), E.c_str(), E.length(), 0);



	    
	  } else if (letter == 'F') {
	    if (debug) {cout << "Letter is F" << endl;}
	    F = headerdata;
	
	    if (boost::regex_search(F.c_str(), match, F_regex)) {
	      RESPONSE_HTTP_VERSION = match[1];
	      RESPONSE_HTTP_STATUS_CODE = match[2];
	      RESPONSE_HTTP_STATUS_TEXT = match[3];
              
	    } else {
	      cerr << "Failed to match F" << endl;
	    }
	    // get integer IDs from the map
            RESPONSE_HTTP_VERSION_ID = ID_from_map(RESPONSE_HTTP_VERSION,http_version_f_map,debug);
            RESPONSE_HTTP_STATUS_CODE_ID = ID_from_map(RESPONSE_HTTP_STATUS_CODE,http_status_code_map,debug);
            RESPONSE_HTTP_STATUS_TEXT_ID = ID_from_map(RESPONSE_HTTP_STATUS_TEXT,http_status_text_map,debug);
	    
	    if (boost::regex_search(F.c_str(), match, F_regex_x_powered_by)) {
	      RESPONSE_X_POWERED_BY = match[1];
	    }
	    RESPONSE_X_POWERED_BY_ID = ID_from_map(RESPONSE_X_POWERED_BY,x_powered_by_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_expires)) {
	      RESPONSE_EXPIRES = match[1];
	    }
	    RESPONSE_EXPIRES_ID = ID_from_map(RESPONSE_EXPIRES,expires_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_cache_control)) {
	      RESPONSE_CACHE_CONTROL = match[1];
	    }
	    RESPONSE_CACHE_CONTROL_ID = ID_from_map(RESPONSE_CACHE_CONTROL,cache_control_f_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_pragma)) {
	      RESPONSE_PRAGMA = match[1];
	    }
	    RESPONSE_PRAGMA_ID = ID_from_map(RESPONSE_PRAGMA,pragma_f_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_vary)) {
	      RESPONSE_VARY = match[1];
	    }
	    RESPONSE_VARY_ID = ID_from_map(RESPONSE_VARY,vary_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_content_encoding)) {
	      RESPONSE_CONTENT_ENCODING = match[1];
	    }
	    RESPONSE_CONTENT_ENCODING_ID = ID_from_map(RESPONSE_CONTENT_ENCODING,content_encoding_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_content_length)) {
	      RESPONSE_CONTENT_LENGTH = match[1];
	    }
	    RESPONSE_CONTENT_LENGTH_ID = ID_from_map(RESPONSE_CONTENT_LENGTH,content_length_f_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_connection)) {
	      RESPONSE_CONNECTION = match[1];
	    }
	    RESPONSE_CONNECTION_ID = ID_from_map(RESPONSE_CONNECTION,connection_f_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_content_type)) {
	      RESPONSE_CONTENT_TYPE = match[1];
	    }
	    RESPONSE_CONTENT_TYPE_ID = ID_from_map(RESPONSE_CONTENT_TYPE,content_type_f_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_status)) {
	      RESPONSE_STATUS = match[1];
	    }
	    RESPONSE_STATUS_ID = ID_from_map(RESPONSE_STATUS,status_map,debug);
            
	    if (boost::regex_search(F.c_str(), match, F_regex_keep_alive)) {
	      RESPONSE_KEEP_ALIVE = match[1];
	    }
	    RESPONSE_KEEP_ALIVE_ID = ID_from_map(RESPONSE_KEEP_ALIVE,keep_alive_map,debug);
	    
	    
	    // bind whole F string
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":F"), F.c_str(), F.length(), 0);	    

	    // bind first statement
            bind_ID (stmt_insert_F, ":RESPONSE_HTTP_VERSION_ID", RESPONSE_HTTP_VERSION_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_HTTP_STATUS_TEXT_ID", RESPONSE_HTTP_STATUS_TEXT_ID, debug);
                        
	    // bind the rest
            bind_ID (stmt_insert_F, ":RESPONSE_X_POWERED_BY_ID", RESPONSE_X_POWERED_BY_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_EXPIRES_ID", RESPONSE_X_POWERED_BY_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_CACHE_CONTROL_ID", RESPONSE_CACHE_CONTROL_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_PRAGMA_ID", RESPONSE_PRAGMA_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_VARY_ID", RESPONSE_VARY_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_CONTENT_ENCODING_ID", RESPONSE_CONTENT_ENCODING_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_CONTENT_LENGTH_ID", RESPONSE_CONTENT_LENGTH_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_CONNECTION_ID", RESPONSE_CONNECTION_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_CONTENT_TYPE_ID", RESPONSE_CONTENT_TYPE_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_STATUS_ID", RESPONSE_STATUS_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_KEEP_ALIVE_ID", RESPONSE_KEEP_ALIVE_ID, debug);
            bind_ID (stmt_insert_F, ":RESPONSE_HTTP_STATUS_CODE_ID", RESPONSE_HTTP_STATUS_CODE_ID, debug);
            
            
            
            
            
	    
	    
	    
	    
	    
	    
	    
	    
	  } else if (letter == 'G') {
	    if (debug) {cout << "Letter is G" << endl;}
	    G = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":G"), G.c_str(), G.length(), 0);	    

	    
	  } else if (letter == 'H') {
	    if (debug) {cout << "Letter is H" << endl;}
	    H = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":H"), H.c_str(), H.length(), 0);	
	    
	    // make a stream object from the H string so that it can be processed line by line
	    std::istringstream streamH(H);
	    string Hline;
	    while (getline(streamH, Hline)) {
	      if (boost::regex_search(Hline.c_str(), match, H_regex_messages)) {
		TRAILER_MESSAGES.append(match[1]);
		TRAILER_MESSAGES.append(string("\n"));
	      }
	    }
	    
	    TRAILER_MESSAGES_ID = ID_from_map(TRAILER_MESSAGES,messages_map,debug);
	    
	    
	    if (boost::regex_search(H.c_str(), match, H_regex_apache_handler)) {
	      TRAILER_APACHE_HANDLER = match[1];
	    }
	    TRAILER_APACHE_HANDLER_ID = ID_from_map(TRAILER_APACHE_HANDLER,apache_handler_map,debug);
            
	    if (boost::regex_search(H.c_str(), match, H_regex_apache_error)) {
	      TRAILER_APACHE_ERROR = match[1];
	    }
	    TRAILER_APACHE_ERROR_ID = ID_from_map(TRAILER_APACHE_ERROR,apache_error_map,debug);
            
	    if (boost::regex_search(H.c_str(), match, H_regex_stopwatch)) {
	      TRAILER_STOPWATCH = match[1];
	    }
	    if (boost::regex_search(H.c_str(), match, H_regex_stopwatch2)) {
	      TRAILER_STOPWATCH2 = match[1];
	    }
	    //if (boost::regex_search(H.c_str(), match, H_regex_response_body_transformed)) {
	    //  TRAILER_RESPONSE_BODY_TRANSFORMED = match[1];
	    //}
	    if (boost::regex_search(H.c_str(), match, H_regex_producer)) {
	      TRAILER_PRODUCER = match[1];
	    }
	    TRAILER_PRODUCER_ID = ID_from_map(TRAILER_PRODUCER,producer_map,debug);
            
	    if (boost::regex_search(H.c_str(), match, H_regex_server)) {
	      TRAILER_SERVER = match[1];
	    }
	    TRAILER_SERVER_ID = ID_from_map(TRAILER_SERVER,server_map,debug);
            
	    if (boost::regex_search(H.c_str(), match, H_regex_engine_mode)) {
	      TRAILER_ENGINE_MODE = match[1];
	    }
	    TRAILER_ENGINE_MODE_ID = ID_from_map(TRAILER_ENGINE_MODE,engine_mode_map,debug);
            
	    if (boost::regex_search(H.c_str(), match, H_regex_action)) {
	      TRAILER_ACTION = match[1];
	    }
	    TRAILER_ACTION_ID = ID_from_map(TRAILER_ACTION,action_map,debug);
            
	    if (boost::regex_search(H.c_str(), match, H_regex_xml_parser_error)) {
	      TRAILER_XML_PARSER_ERROR = match[1];
	    }
	    TRAILER_XML_PARSER_ERROR_ID = ID_from_map(TRAILER_XML_PARSER_ERROR,xml_parser_error_map,debug);
	    
	    // bind values for table H
	    sqlite3_bind_text(stmt_insert_H, sqlite3_bind_parameter_index(stmt_insert_H, ":TRAILER_STOPWATCH"), TRAILER_STOPWATCH.c_str(), TRAILER_STOPWATCH.length(), 0);
	    sqlite3_bind_text(stmt_insert_H, sqlite3_bind_parameter_index(stmt_insert_H, ":TRAILER_STOPWATCH2"), TRAILER_STOPWATCH2.c_str(), TRAILER_STOPWATCH2.length(), 0);
	    
            
            // bind ID ints
            bind_ID (stmt_insert_H, ":TRAILER_MESSAGES_ID", TRAILER_MESSAGES_ID, debug);
            bind_ID (stmt_insert_H, ":TRAILER_APACHE_HANDLER_ID", TRAILER_APACHE_HANDLER_ID, debug);
            bind_ID (stmt_insert_H, ":TRAILER_PRODUCER_ID", TRAILER_PRODUCER_ID, debug);
            bind_ID (stmt_insert_H, ":TRAILER_SERVER_ID", TRAILER_SERVER_ID, debug);
            bind_ID (stmt_insert_H, ":TRAILER_ENGINE_MODE_ID", TRAILER_ENGINE_MODE_ID, debug);
            bind_ID (stmt_insert_H, ":TRAILER_ACTION_ID", TRAILER_ACTION_ID, debug);
            bind_ID (stmt_insert_H, ":TRAILER_XML_PARSER_ERROR_ID", TRAILER_XML_PARSER_ERROR_ID, debug);
            bind_ID (stmt_insert_H, ":TRAILER_APACHE_ERROR_ID", TRAILER_APACHE_ERROR_ID, debug);
            
            

	    
	    // if the next operation is performed on the string H, then the data for H in the database becomes corrupted. Is sregex_iterator moving the start of H?
	    string H2 = H;
	    
	    
	    // search for rule IDs and bind integers
	    boost::sregex_iterator m1(H2.begin(), H2.end(), H_regex_any_rule);
	    boost::sregex_iterator m2;
	    std::set<std::string> ruleIDsSet; // use a set to hold the IDs so no duplicates are created
	    std::vector < string > ruleIDsVector; // use a vector to hold the IDs and preserve duplicates

	    
	    
	    
	    
	    // for each match, add the submatch (six digit rule ID) to a set and a vector
	    for (; m1 !=m2; ++m1) {
	      ruleIDsSet.insert ( m1->str(1) ); // rule IDs set (no duplicates)
	      ruleIDsVector.push_back( m1->str(1) ); // rule IDs vector (duplicates)
	    }
	    
	    
	    // print the unique ID followed by a unique list of the rule IDs matched
	    if (debug) {
	      cout << UNIQUE_ID << ": rules matched: ";
	      for (const auto &id : ruleIDsSet) {
		cout << id << ", ";
	      }
	      cout << endl;
	    }


	    // now count the number of times each individual rule was matched
	    map<string, size_t> ruleIDCountMap; // empty map from id string to size_t
	    int ids = ruleIDsVector.size(); // get size of vector holding ruleIDs
	    
	    // use "word count" program like cpp primer p.421
	    for ( int id = 0; id < ids; ++id) {
	      // increment the counter for the id
	      ++ruleIDCountMap[ruleIDsVector[id]];
	    }
	    
	    // print results
	    if (debug) {
	      for (const auto &id : ruleIDCountMap) {
		cout << id.first << " counted " << id.second
		  << ((id.second >1 ) ? " times" : " time") << endl;
	      }
	    }
	    
	    
	    // bind scores to the scores table
	    for (const auto &id : ruleIDCountMap) {
                // multiply the count number by the weighting to get the score for that rule and add increase the integer for the relevant table
                // look up weighting in the rulesdata map
                int weighting;
                string ruleno = id.first;
                auto pos = ruledatamap.find(ruleno);
                if (pos == ruledatamap.end()) { 
                    cerr << "rule " << ruleno << " was not found in the map" << endl;
                } else {
                    weighting = (pos->second).second; // second part of value pair in rulesdata map (weighting)
                }
                // calculate the score
                int rulescore = id.second * weighting; // number of matches multiplied by weighting
                
                // fetch the relevant rulefile name (string) for this rule
                string rulefilename = (ruledatamap.find(ruleno)->second).first;
                if (debug) {cout << "Rule filename is " << rulefilename << endl;}
                
                // look up the counter associated with this string
                int currentscore = rulefiletocountermap[rulefilename];
                if (debug) {cout << "Counter for this rulefile is currently " << currentscore << endl;}
                
                // set new score
                rulefiletocountermap[rulefilename] = currentscore + rulescore;
            }
            
            
            
            
            
            
            // bind values to the ruleID score table
            int totalscore;
            for (const auto &rf : rulefiletocountermap) {
                
                string rulefile = rf.first;
                string colonrulefile = ":" + rulefile; 
                int score = rf.second;
                totalscore = totalscore + score;
                
                int rc_bind = sqlite3_bind_int(stmt_insert_scores, sqlite3_bind_parameter_index(stmt_insert_scores, colonrulefile.c_str()), score);
                
                if (rc_bind != SQLITE_OK) {
		  cerr << UNIQUE_ID << ": error binding score for " << rulefile << " . Code " << rc_bind << " description: " << sqlite3_errmsg(db) << endl;
		} else {
		  if (debug) {cout << UNIQUE_ID << ": score for " << rulefile << " bound successfully" << endl;}
		}
		
            }
            
            int rc_totalbind = sqlite3_bind_int(stmt_insert_scores, sqlite3_bind_parameter_index(stmt_insert_scores, ":total_score"), totalscore);
                
            if (rc_totalbind != SQLITE_OK) {
                cerr << UNIQUE_ID << ": error binding total score. Code " << rc_totalbind << " description: " << sqlite3_errmsg(db) << endl;
            } else {
                if (debug) {cout << UNIQUE_ID << ": total score bound successfully" << endl;}
            }
            
            
            // reset all of the counters
            // NB: cbegin is for const iterator to beginning of a map, begin is just iterator to beginning (not const). cbegin can't be used to modify content of map pointed to
            auto map_it = rulefiletocountermap.begin();
            while (map_it != rulefiletocountermap.end()) {
                map_it->second=0;
                ++map_it;
            }
            totalscore = 0;
            
	    
	    
            
            // bind the number of matches for each rule to the relevant statement
            for (const auto &pos : ruleIDmap) {
                
                // get ID string
                string IDstring = pos.first;
                
                sqlite3_stmt *statement = *get<0>((ruleIDmap.find(IDstring))->second);
                
                string colonnumber = ":" + IDstring;
                
                int rc_bind;
                int num_matches;
                
                auto pos2 = ruleIDCountMap.find(IDstring);
                if (pos2 == ruleIDCountMap.end()) { // if id does not exist as a key in the counter map, then there were no matches
                    num_matches = 0;
                } else {
                    num_matches = pos2->second;
                }
                rc_bind = sqlite3_bind_int(statement, sqlite3_bind_parameter_index(statement, colonnumber.c_str()), num_matches);
                
                if (rc_bind != SQLITE_OK) {
		  cerr << UNIQUE_ID << ": error binding values for " << IDstring << " . Code " << rc_bind << " description: " << sqlite3_errmsg(db) << endl;
		} else {
		  if (debug) {cout << UNIQUE_ID << ": values for " << IDstring << " bound successfully" << endl;}
		}
            }
            
	    
	    
	    
	    
	    
	    
	    
	    
	    
	  } else if (letter == 'I') {
	    if (debug) {cout << "Letter is I" << endl;}
	    I = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":I"), I.c_str(), I.length(), 0);

	    
	  } else if (letter == 'J') {
	    if (debug) {cout << "Letter is J" << endl;}
	    J = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":J"), J.c_str(), J.length(), 0);

	    
	  } else if (letter == 'K') {
	    if (debug) {cout << "Letter is K" << endl;}
	    K = headerdata;
	    sqlite3_bind_text(stmt_insert_main, sqlite3_bind_parameter_index(stmt_insert_main, ":K"), K.c_str(), K.length(), 0);

	    
	  } else if (letter == 'Z') {
	    if (debug) {cout << "Letter is Z, committing to database" << endl;}
	    	    

	    // commit data to the database
	    for (const auto &s : prepared_statements_map) {
	      int step_rc = sqlite3_step(*get<1>(s.second));
	      if (step_rc != SQLITE_OK && step_rc != SQLITE_DONE) {
		cerr << UNIQUE_ID << ": SQLite error stepping " << s.first << " . Code " << step_rc << ": " << sqlite3_errmsg(db) << endl;
	      } else {
		if (debug) {cout << UNIQUE_ID << ": " << s.first << " was stepped successfully" << endl;}
	      }
	    }
	    	    
	    // reset all of the prepared statements ready to be re-executed
	    for (const auto &s : prepared_statements_map) {
	      rc = sqlite3_reset(*get<1>(s.second));
	      if( rc != SQLITE_OK ){
		cerr << "SQL error resetting " << s.first << " prepared statement" << endl;
		cerr << "The error was: "<< sqlite3_errmsg(db) << endl;
	      } else {
		if (debug) {cout << "Prepared statement " << s.first << " was reset successfully" << endl;}
	      }
	    }
	    
	    // clear bindings for each prepared statement
	    for (const auto &s : prepared_statements_map) {
	      rc = sqlite3_clear_bindings(*get<1>(s.second));
	      if( rc != SQLITE_OK ){
		cerr << "SQL error clearing the bindings for " << s.first << "prepared statement" << endl;
		cerr << "The error was: "<< sqlite3_errmsg(db) << endl;
	      } else {
		if (debug) {cout << "Bindings for " << s.first << " were cleared successfully" << endl;}
	      }
	    }
	    

	    // increment record counter
	    ++recordCounter;
	    
	    if (debug) {cout << "Resetting strings to empty" << endl;}
	    // clear main strings
	    UNIQUE_ID=HEADER=A=B=C=D=E=F=G=H="";
	    
	    // clear A strings
	    TIMESTAMP=UNIXTIME=SOURCE_IP=SOURCE_PORT=DESTINATION_IP=DESTINATION_PORT="";
	    REQUEST_METHOD=REQUEST_URI=REQUEST_HTTP_VERSION="";
	    
	    // clear B strings
	    REQUEST_HOST=REQUEST_CONNECTION=REQUEST_ACCEPT=REQUEST_USER_AGENT=REQUEST_DNT=REQUEST_REFERRER=REQUEST_ACCEPT_ENCODING=REQUEST_ACCEPT_LANGUAGE=REQUEST_COOKIE=REQUEST_X_REQUESTED_WITH=REQUEST_CONTENT_TYPE=REQUEST_CONTENT_LENGTH=REQUEST_PROXY_CONNECTION=REQUEST_ACCEPT_CHARSET=REQUEST_UA_CPU=REQUEST_X_FORWARDED_FOR=REQUEST_CACHE_CONTROL=REQUEST_VIA=REQUEST_IF_MODIFIED_SINCE=REQUEST_IF_NONE_MATCH=REQUEST_PRAGMA="";
	    RESPONSE_HTTP_VERSION=RESPONSE_HTTP_STATUS_CODE=RESPONSE_HTTP_STATUS_TEXT=RESPONSE_X_POWERED_BY=RESPONSE_EXPIRES=RESPONSE_CACHE_CONTROL=RESPONSE_PRAGMA=RESPONSE_VARY=RESPONSE_CONTENT_ENCODING=RESPONSE_CONTENT_LENGTH=RESPONSE_CONNECTION= RESPONSE_CONTENT_TYPE=RESPONSE_STATUS=RESPONSE_KEEP_ALIVE="";
	    
	    // clear H strings
	    TRAILER_MESSAGES=TRAILER_APACHE_HANDLER=TRAILER_APACHE_ERROR=TRAILER_STOPWATCH=TRAILER_STOPWATCH2=TRAILER_PRODUCER=TRAILER_SERVER=TRAILER_ENGINE_MODE=TRAILER_ACTION=TRAILER_XML_PARSER_ERROR="";
            
	    
	  }
	  break; // stop reading file
	} // end of "if line == endline"
      } // end of "while (getline(in, linedata))
    } // end of for loop looping through results vector
    
    
    // create sql statements for committing to database
    // A
    const char * sql_source_ip_ID = "INSERT OR IGNORE INTO source_ip (source_ip_id, source_ip) VALUES (:id, :value);";
    const char * sql_source_port_ID = "INSERT OR IGNORE INTO source_port (source_port_id, source_port) VALUES (:id, :value);";
    const char * sql_destination_ip_ID = "INSERT OR IGNORE INTO destination_ip (destination_ip_id, destination_ip) VALUES (:id, :value);";
    const char * sql_destination_port_ID = "INSERT OR IGNORE INTO destination_port (destination_port_id, destination_port) VALUES (:id, :value);";

    // B
    const char * sql_request_method_ID = "INSERT OR IGNORE INTO request_method (request_method_id, request_method) VALUES (:id, :value);";
    const char * sql_uri_ID = "INSERT OR IGNORE INTO uri (uri_id, uri) VALUES (:id, :value);";
    const char * sql_http_version_b_ID = "INSERT OR IGNORE INTO http_version_b (http_version_b_id, http_version_b) VALUES (:id, :value);";
    const char * sql_hosts_ID = "INSERT OR IGNORE INTO hosts (host_id, host) VALUES (:id, :value);";
    const char * sql_connection_b_ID = "INSERT OR IGNORE INTO connection_b (connection_b_id, connection_b) VALUES (:id, :value);";
    const char * sql_accept_ID = "INSERT OR IGNORE INTO accept (accept_id, accept) VALUES (:id, :value);";
    const char * sql_user_agent_ID = "INSERT OR IGNORE INTO user_agent (user_agent_id, user_agent) VALUES (:id, :value);";
    const char * sql_dnt_ID = "INSERT OR IGNORE INTO dnt (dnt_id, dnt) VALUES (:id, :value);";
    const char * sql_referrer_ID = "INSERT OR IGNORE INTO referrer (referrer_id, referrer) VALUES (:id, :value);";
    const char * sql_accept_encoding_ID = "INSERT OR IGNORE INTO accept_encoding (accept_encoding_id, accept_encoding) VALUES (:id, :value);";
    const char * sql_accept_language_ID = "INSERT OR IGNORE INTO accept_language (accept_language_id, accept_language) VALUES (:id, :value);";
    const char * sql_cookie_ID = "INSERT OR IGNORE INTO cookie (cookie_id, cookie) VALUES (:id, :value);";
    const char * sql_x_requested_with_ID = "INSERT OR IGNORE INTO x_requested_with (x_requested_with_id, x_requested_with) VALUES (:id, :value);";
    const char * sql_content_type_b_ID = "INSERT OR IGNORE INTO content_type_b (content_type_b_id, content_type_b) VALUES (:id, :value);";
    const char * sql_content_length_b_ID = "INSERT OR IGNORE INTO content_length_b (content_length_b_id, content_length_b) VALUES (:id, :value);";
    const char * sql_proxy_connection_ID = "INSERT OR IGNORE INTO proxy_connection (proxy_connection_id, proxy_connection) VALUES (:id, :value);";
    const char * sql_accept_charset_ID = "INSERT OR IGNORE INTO accept_charset (accept_charset_id, accept_charset) VALUES (:id, :value);";
    const char * sql_ua_cpu_ID = "INSERT OR IGNORE INTO ua_cpu (ua_cpu_id, ua_cpu) VALUES (:id, :value);";
    const char * sql_x_forwarded_for_ID = "INSERT OR IGNORE INTO x_forwarded_for (x_forwarded_for_id, x_forwarded_for) VALUES (:id, :value);";
    const char * sql_cache_control_b_ID = "INSERT OR IGNORE INTO cache_control_b (cache_control_b_id, cache_control_b) VALUES (:id, :value);";
    const char * sql_via_ID = "INSERT OR IGNORE INTO via (via_id, via) VALUES (:id, :value);";
    const char * sql_if_modified_since_ID = "INSERT OR IGNORE INTO if_modified_since (if_modified_since_id, if_modified_since) VALUES (:id, :value);";
    const char * sql_if_none_match_ID = "INSERT OR IGNORE INTO if_none_match (if_none_match_id, if_none_match) VALUES (:id, :value);";
    const char * sql_pragma_b_ID = "INSERT OR IGNORE INTO pragma_b (pragma_b_id, pragma_b) VALUES (:id, :value);";
    
    // F
    const char * sql_http_version_f_ID = "INSERT OR IGNORE INTO http_version_f (http_version_f_id, http_version_f) VALUES (:id, :value);";
    const char * sql_http_status_code_ID = "INSERT OR IGNORE INTO http_status_code (http_status_code_id, http_status_code) VALUES (:id, :value);";
    const char * sql_http_status_text_ID = "INSERT OR IGNORE INTO http_status_text (http_status_text_id, http_status_text) VALUES (:id, :value);";
    const char * sql_x_powered_by_ID = "INSERT OR IGNORE INTO x_powered_by (x_powered_by_id, x_powered_by) VALUES (:id, :value);";
    const char * sql_expires_ID = "INSERT OR IGNORE INTO expires (expires_id, expires) VALUES (:id, :value);";
    const char * sql_cache_control_f_ID = "INSERT OR IGNORE INTO cache_control_f (cache_control_f_id, cache_control_f) VALUES (:id, :value);";
    const char * sql_pragma_f_ID = "INSERT OR IGNORE INTO pragma_f (pragma_f_id, pragma_f) VALUES (:id, :value);";
    const char * sql_vary_ID = "INSERT OR IGNORE INTO vary (vary_id, vary) VALUES (:id, :value);";
    const char * sql_content_encoding_ID = "INSERT OR IGNORE INTO content_encoding (content_encoding_id, content_encoding) VALUES (:id, :value);";
    const char * sql_content_length_f_ID = "INSERT OR IGNORE INTO content_length_f (content_length_f_id, content_length_f) VALUES (:id, :value);";
    const char * sql_connection_f_ID = "INSERT OR IGNORE INTO connection_f (connection_f_id, connection_f) VALUES (:id, :value);";
    const char * sql_content_type_f_ID = "INSERT OR IGNORE INTO content_type_f (content_type_f_id, content_type_f) VALUES (:id, :value);";
    const char * sql_status_ID = "INSERT OR IGNORE INTO status (status_id, status) VALUES (:id, :value);";
    const char * sql_keep_alive_ID = "INSERT OR IGNORE INTO keep_alive (keep_alive_id, keep_alive) VALUES (:id, :value);";

    // H
    const char * sql_messages_ID = "INSERT OR IGNORE INTO messages (messages_id, messages) VALUES (:id, :value);";
    const char * sql_apache_handler_ID = "INSERT OR IGNORE INTO apache_handler (apache_handler_id, apache_handler) VALUES (:id, :value);";
    const char * sql_producer_ID = "INSERT OR IGNORE INTO producer (producer_id, producer) VALUES (:id, :value);";
    const char * sql_server_ID = "INSERT OR IGNORE INTO server (server_id, server) VALUES (:id, :value);";
    const char * sql_engine_mode_ID = "INSERT OR IGNORE INTO engine_mode (engine_mode_id, engine_mode) VALUES (:id, :value);";
    const char * sql_action_ID = "INSERT OR IGNORE INTO action (action_id, action) VALUES (:id, :value);";
    const char * sql_apache_error_ID = "INSERT OR IGNORE INTO apache_error (apache_error_id, apache_error) VALUES (:id, :value);";
    const char * sql_xml_parser_error_ID = "INSERT OR IGNORE INTO xml_parser_error (xml_parser_error_id, xml_parser_error) VALUES (:id, :value);";
    
    
    
    

    
    
    
    
    // commit ID maps to database
    // A
    commit_maps(db, sql_source_ip_ID, source_ip_map, debug);
    commit_maps(db, sql_source_port_ID, source_port_map, debug);
    commit_maps(db, sql_destination_ip_ID, destination_ip_map, debug);
    commit_maps(db, sql_destination_port_ID, destination_port_map, debug);

    // B
    commit_maps(db, sql_request_method_ID, request_method_map, debug);
    commit_maps(db, sql_uri_ID, uri_map, debug);
    commit_maps(db, sql_http_version_b_ID, http_version_b_map, debug);
    commit_maps(db, sql_hosts_ID, hosts_map, debug);
    commit_maps(db, sql_connection_b_ID, connection_b_map, debug);
    commit_maps(db, sql_accept_ID, accept_map, debug);
    commit_maps(db, sql_user_agent_ID, user_agent_map, debug);    
    commit_maps(db, sql_dnt_ID, dnt_map, debug);
    commit_maps(db, sql_referrer_ID, referrer_map, debug);
    commit_maps(db, sql_accept_encoding_ID, accept_encoding_map, debug);
    commit_maps(db, sql_accept_language_ID, accept_language_map, debug);
    commit_maps(db, sql_cookie_ID, cookie_map, debug);
    commit_maps(db, sql_x_requested_with_ID, x_requested_with_map, debug);
    commit_maps(db, sql_content_type_b_ID, content_type_b_map, debug);
    commit_maps(db, sql_content_length_b_ID, content_length_b_map, debug);
    commit_maps(db, sql_proxy_connection_ID, proxy_connection_map, debug);
    commit_maps(db, sql_accept_charset_ID, accept_charset_map, debug);
    commit_maps(db, sql_ua_cpu_ID, ua_cpu_map, debug);
    commit_maps(db, sql_x_forwarded_for_ID, x_forwarded_for_map, debug);
    commit_maps(db, sql_cache_control_b_ID, cache_control_b_map, debug);
    commit_maps(db, sql_via_ID, via_map, debug);
    commit_maps(db, sql_if_modified_since_ID, if_modified_since_map, debug);
    commit_maps(db, sql_if_none_match_ID, if_none_match_map, debug);
    commit_maps(db, sql_pragma_b_ID, pragma_b_map, debug);
    
    // F
    commit_maps(db, sql_http_version_f_ID, http_version_f_map, debug);
    commit_maps(db, sql_http_status_code_ID, http_status_code_map, debug);
    commit_maps(db, sql_http_status_text_ID, http_status_text_map, debug);
    commit_maps(db, sql_x_powered_by_ID, x_powered_by_map, debug);
    commit_maps(db, sql_expires_ID, expires_map, debug);
    commit_maps(db, sql_cache_control_f_ID, cache_control_f_map, debug);
    commit_maps(db, sql_pragma_f_ID, pragma_f_map, debug);
    commit_maps(db, sql_vary_ID, vary_map, debug);
    commit_maps(db, sql_content_encoding_ID, content_encoding_map, debug);
    commit_maps(db, sql_content_length_f_ID, content_length_f_map, debug);
    commit_maps(db, sql_connection_f_ID, connection_f_map, debug);
    commit_maps(db, sql_content_type_f_ID, content_type_f_map, debug);
    commit_maps(db, sql_status_ID, status_map, debug);
    commit_maps(db, sql_keep_alive_ID, keep_alive_map, debug);

    // H
    commit_maps(db, sql_messages_ID, messages_map, debug);
    commit_maps(db, sql_apache_handler_ID, apache_handler_map, debug);
    commit_maps(db, sql_producer_ID, producer_map, debug);
    commit_maps(db, sql_server_ID, server_map, debug);
    commit_maps(db, sql_engine_mode_ID, engine_mode_map, debug);
    commit_maps(db, sql_action_ID, action_map, debug);
    commit_maps(db, sql_apache_error_ID, apache_error_map, debug);
    commit_maps(db, sql_xml_parser_error_ID, xml_parser_error_map, debug);



    
    
    sqlite3_exec(db,"END TRANSACTION",0,0,0);
    
    // now that we are done with these statements they can be destroyed to free resources
    for (const auto &s : prepared_statements_map) {
      rc = sqlite3_finalize(*get<1>(s.second));
      if( rc != SQLITE_OK ){
	cerr << "SQL error finalizing " << s.first << " statement. The error was:" << endl;
	cerr << sqlite3_errmsg(db) << endl;
      } else {
	if (debug) {cout << "Finalized " << s.first << " statement successfully" << endl;}
      }
    }
    

  } 
  end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end-start;
  double rate = recordCounter / elapsed_seconds.count();
  cout << "Processed " << recordCounter << " records in " << elapsed_seconds.count() << " seconds (" << rate << "/s)." << endl;
  return 0;
}