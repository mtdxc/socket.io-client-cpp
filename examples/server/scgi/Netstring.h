#pragma once

#include <string>
#include <map>

namespace utils_private
{
using std::map;
using std::string;

template <class T>
class Netstring 
{
    public:
        typedef map<string, string> value_t;
        // len:key1 value1 key2 value2 ...
        Netstring(const T& input) 
        :m_input(input)
        {
            auto doublecolonptr = std::find(m_input.begin(), m_input.end(), ':');
            string sizeStr(m_p, doublecolonptr);
            m_size = atoi(sizeStr.c_str());
            typename T::const_iterator m_p = doublecolonptr + 1;

            auto endstr = m_p + m_size;
            while (m_p < endstr && m_p != 0 ) {
                string tag(m_p);
                m_p += tag.size() + 1;
                string value(m_p);
                m_p += value.size() + 1;

                m_values[tag] = value;
            }
        }

        operator value_t()
        {
            return m_values;
        }

    private:
        void readSize() {
        }

        map<string, string> m_values;
        const T& m_input;
        size_t m_size;
};
}

namespace utils {
    using utils_private::Netstring;

    template <class T>
    std::map<std::string, std::string> netstring2map(const T& input) {
        return Netstring<T>(input);
    }
}
