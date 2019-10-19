#ifndef ENVIRONMENT_HPP
#define ENVIRONMENT_HPP

class Environment
{
private:
    std::vector<std::pair<std::wstring, std::wstring>> m_Pairs;

public:
    void set(const std::string &var)
    {
        const char *value = getenv(var.c_str());
        if (value != nullptr)
            set(var, value);
    }

    void set(const std::string &var, const std::string &value)
    {
        m_Pairs.push_back(std::make_pair(mbsToWcs(var), mbsToWcs(value)));
    }

    bool hasVar(const std::wstring &var)
    {
        for (const auto &pair : m_Pairs)
        {
            if (pair.first == var)
                return true;
        }
        return false;
    }

    const std::vector<std::pair<std::wstring, std::wstring>> &pairs()
    {
        return m_Pairs;
    }
};

#endif /* ENVIRONMENT_HPP */
