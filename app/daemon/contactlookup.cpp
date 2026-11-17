#include <sqlite3.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static std::string digitsOnly(const std::string &text)
{
    std::string out;
    for (char c : text) {
        if (c >= '0' && c <= '9')
            out.push_back(c);
    }
    return out;
}

static bool allowedNumber(const std::string &text)
{
    int digits = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9')
            ++digits;
        else if (c != '+' && c != ' ' && c != '-' && c != '(' && c != ')')
            return false;
    }
    return digits >= 5;
}

static void findDatabases(const std::string &dir, std::vector<std::string> &out)
{
    DIR *handle = opendir(dir.c_str());
    if (!handle)
        return;
    while (dirent *entry = readdir(handle)) {
        if (entry->d_name[0] == '.')
            continue;
        const std::string path = dir + "/" + entry->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            findDatabases(path, out);
        else if (path.size() > 3 && path.compare(path.size() - 3, 3, ".db") == 0)
            out.push_back(path);
    }
    closedir(handle);
}

static std::string columnText(sqlite3_stmt *stmt)
{
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    return text ? reinterpret_cast<const char *>(text) : std::string();
}

static std::string queryName(sqlite3 *db, const char *sql, const std::string &digits,
                             const std::string &tail)
{
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
        return std::string();
    sqlite3_bind_text(stmt, 1, digits.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, tail.c_str(), -1, SQLITE_TRANSIENT);
    std::string name;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        name = columnText(stmt);
    sqlite3_finalize(stmt);
    return name;
}

static std::string lookup(sqlite3 *db, const std::string &digits, const std::string &tail)
{
    const char *aggregate =
            "SELECT D.displayLabel "
            "FROM PhoneNumbers P "
            "JOIN DisplayLabels D ON D.contactId = P.contactId "
            "JOIN Contacts C ON C.contactId = P.contactId "
            "WHERE C.isDeactivated = 0 "
            "AND length(P.normalizedNumber) >= 6 "
            "AND (P.normalizedNumber = ?1 "
            "     OR (?2 != '' AND (P.normalizedNumber LIKE '%' || ?2 "
            "          OR ?1 LIKE '%' || P.normalizedNumber))) "
            "ORDER BY (C.collectionId = 1) DESC, length(P.normalizedNumber) DESC "
            "LIMIT 1";
    std::string name = queryName(db, aggregate, digits, tail);
    if (!name.empty())
        return name;

    const char *plain =
            "SELECT D.displayLabel "
            "FROM PhoneNumbers P "
            "JOIN DisplayLabels D ON D.contactId = P.contactId "
            "WHERE length(P.normalizedNumber) >= 6 "
            "AND (P.normalizedNumber = ?1 "
            "     OR (?2 != '' AND (P.normalizedNumber LIKE '%' || ?2 "
            "          OR ?1 LIKE '%' || P.normalizedNumber))) "
            "ORDER BY length(P.normalizedNumber) DESC "
            "LIMIT 1";
    return queryName(db, plain, digits, tail);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 0;
    const std::string raw(argv[1]);
    if (!allowedNumber(raw))
        return 0;
    const std::string digits = digitsOnly(raw);
    if (digits.size() < 5)
        return 0;
    const std::string tail = digits.size() > 8 ? digits.substr(digits.size() - 8) : digits;

    const char *home = std::getenv("HOME");
    if (!home || !home[0])
        return 0;

    std::vector<std::string> databases;
    findDatabases(std::string(home) + "/.local/share/system/privileged/Contacts", databases);
    for (const std::string &path : databases) {
        sqlite3 *db = 0;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, 0) != SQLITE_OK) {
            if (db)
                sqlite3_close(db);
            continue;
        }
        sqlite3_busy_timeout(db, 500);
        const std::string name = lookup(db, digits, tail);
        sqlite3_close(db);
        if (!name.empty()) {
            std::cout << name << std::endl;
            return 0;
        }
    }
    return 0;
}
