/* http_header.cc
   Mathieu Stefani, 19 August 2015
   
   Implementation of common HTTP headers described by the RFC
*/

#include "http_header.h"
#include "common.h"
#include "http.h"
#include <stdexcept>
#include <iterator>
#include <cstring>
#include <iostream>

using namespace std;

namespace Net {

namespace Http {

namespace Header {

const char* encodingString(Encoding encoding) {
    switch (encoding) {
    case Encoding::Gzip:
        return "gzip";
    case Encoding::Compress:
        return "compress";
    case Encoding::Deflate:
        return "deflate";
    case Encoding::Identity:
        return "identity";
    case Encoding::Unknown:
        return "unknown";
    }

    unreachable();
}

void
Header::parse(const std::string& data) {
    parseRaw(data.c_str(), data.size());
}

void
Header::parseRaw(const char *str, size_t len) {
    parse(std::string(str, len));
}

void
Allow::parseRaw(const char* str, size_t len) {
}

void
Allow::write(std::ostream& os) const {
    /* This puts an extra ',' at the end :/
    std::copy(std::begin(methods_), std::end(methods_),
              std::ostream_iterator<Http::Method>(os, ", "));
    */

    for (std::vector<Http::Method>::size_type i = 0; i < methods_.size(); ++i) {
        os << methods_[i];
        if (i < methods_.size() - 1) os << ", ";
    }
}

void
Allow::addMethod(Http::Method method) {
    methods_.push_back(method);
}

void
Allow::addMethods(std::initializer_list<Method> methods) {
    std::copy(std::begin(methods), std::end(methods), std::back_inserter(methods_));
}

void
Allow::addMethods(const std::vector<Http::Method>& methods)
{
    std::copy(std::begin(methods), std::end(methods), std::back_inserter(methods_));
}

CacheControl::CacheControl(Http::CacheDirective directive)
{
    directives_.push_back(directive);
}

void
CacheControl::parseRaw(const char* str, size_t len) {
    using Http::CacheDirective;

    auto eof = [&](const char *p) {
        return p - str == len;
    };

    struct DirectiveValue {
        const char* const str;
        const size_t size;
        CacheDirective::Directive repr;
    };

#define VALUE(divStr, enumValue) { divStr, sizeof(divStr) - 1, CacheDirective::enumValue }

    static constexpr DirectiveValue TrivialDirectives[] = {
        VALUE("no-cache"        , NoCache        ),
        VALUE("no-store"        , NoStore        ),
        VALUE("no-transform"    , NoTransform    ),
        VALUE("only-if-cached"  , OnlyIfCached   ),
        VALUE("public"          , Public         ),
        VALUE("private"         , Private        ),
        VALUE("must-revalidate" , MustRevalidate ),
        VALUE("proxy-revalidate", ProxyRevalidate)
    };

    static constexpr DirectiveValue TimedDirectives[] = {
        VALUE("max-age"  , MaxAge  ),
        VALUE("max-stale", MaxStale),
        VALUE("min-fresh", MinFresh),
        VALUE("s-maxage" , SMaxAge )
    };

#undef VALUE

    const char *begin = str;
    auto memsize = [&](size_t s) {
        return std::min(s, len - (begin - str));
    };

    do {

        bool found = false;
        // First scan trivial directives
        for (const auto& d: TrivialDirectives) {
            if (memcmp(begin, d.str, memsize(d.size)) == 0) {
                directives_.push_back(CacheDirective(d.repr));
                begin += d.size;
                found = true;
                break;
            }
        }

        // Not found, let's try timed directives
        if (!found) {
            for (const auto& d: TimedDirectives) {
                if (memcmp(begin, d.str, memsize(d.size)) == 0) {
                    const char *p = static_cast<const char *>(memchr(begin, '=', memsize(len)));
                    if (p == NULL) {
                        throw std::runtime_error("Invalid caching directive, missing delta-seconds");
                    }
                    char *end;
                    int secs = strtol(p + 1, &end, 10);
                    if (!eof(end) && *end != ',') {
                        throw std::runtime_error("Invalid caching directive, malformated delta-seconds");
                    }
                    directives_.push_back(CacheDirective(d.repr, std::chrono::seconds(secs)));
                    begin = end;
                    found = true;
                    break;
                }
            }
        }

        if (!eof(begin)) {
            if (*begin != ',')
                throw std::runtime_error("Invalid caching directive, expected a comma");

            while (!eof(begin) && *begin == ',' || *begin == ' ') ++begin;
        }

    } while (!eof(begin));

}

void
CacheControl::write(std::ostream& os) const {
    using Http::CacheDirective;

    auto directiveString = [](CacheDirective directive) -> const char* const {
        switch (directive.directive()) {
            case CacheDirective::NoCache:
                return "no-cache";
            case CacheDirective::NoStore:
                return "no-store";
            case CacheDirective::NoTransform:
                return "no-transform";
            case CacheDirective::OnlyIfCached:
                return "only-if-cached";
            case CacheDirective::Public:
                return "public";
            case CacheDirective::Private:
                return "private";
            case CacheDirective::MustRevalidate:
                return "must-revalidate";
            case CacheDirective::ProxyRevalidate:
                return "proxy-revalidate";
            case CacheDirective::MaxAge:
                return "max-age";
            case CacheDirective::MaxStale:
                return "max-stale";
            case CacheDirective::MinFresh:
                return "min-fresh";
            case CacheDirective::SMaxAge:
                return "s-maxage";
        }
    };

    auto hasDelta = [](CacheDirective directive) {
        switch (directive.directive()) {
            case CacheDirective::MaxAge:
            case CacheDirective::MaxStale:
            case CacheDirective::MinFresh:
            case CacheDirective::SMaxAge:
                return true;
        }
        return false;
    };

    for (std::vector<CacheDirective>::size_type i = 0; i < directives_.size(); ++i) {
        const auto& d = directives_[i];
        os << directiveString(d);
        if (hasDelta(d)) {
            auto delta = d.delta();
            if (delta.count() > 0) {
                os << "=" << delta.count();
            }
        }

        if (i < directives_.size() - 1) {
            os << ", ";
        }
    }


}

void
CacheControl::addDirective(Http::CacheDirective directive) {
    directives_.push_back(directive);
}

void
CacheControl::addDirectives(const std::vector<Http::CacheDirective>& directives) {
    std::copy(std::begin(directives), std::end(directives), std::back_inserter(directives_));
}

void
ContentLength::parse(const std::string& data) {
    try {
        size_t pos;
        uint64_t val = std::stoi(data, &pos);
        if (pos != 0) {
        }

        value_ = val;
    } catch (const std::invalid_argument& e) {
    }
}

void
ContentLength::write(std::ostream& os) const {
    os << value_;
}

void
Host::parse(const std::string& data) {
    auto pos = data.find(':');
    if (pos != std::string::npos) {
        std::string h = data.substr(0, pos);
        int16_t p = std::stoi(data.substr(pos + 1));

        host_ = h;
        port_ = p;
    } else {
        host_ = data;
        port_ = 80;
    }
}

void
Host::write(std::ostream& os) const {
    os << host_;
    if (port_ != -1) {
        os << ":" << port_;
    }
}

void
UserAgent::parse(const std::string& data) {
    ua_ = data;
}

void
UserAgent::write(std::ostream& os) const {
    os << ua_;
}

void
Accept::parseRaw(const char *str, size_t len) {

    auto remaining = [&](const char* p) {
        return len - (p - str);
    };

    auto eof = [&](const char *p) {
        return remaining(p) == 0;
    };

    const char *p = static_cast<const char *>(memchr(str, ',', len));
    const char *begin = str;
    if (p == NULL) {
        mediaRange_.push_back(Mime::MediaType::fromRaw(str, len));
    } else {
        do {

            const size_t mimeLen = p - begin;
            mediaRange_.push_back(Mime::MediaType::fromRaw(begin, mimeLen));

            while (!eof(p) && (*p == ',' || *p == ' ')) ++p;
            if (eof(p)) throw std::runtime_error("Invalid format for Accept header");
            begin = p;

            p = static_cast<const char *>(memchr(p, ',', remaining(p)));

        } while (p != NULL);

        mediaRange_.push_back(Mime::MediaType::fromRaw(begin, remaining(begin)));
    }

}

void
Accept::write(std::ostream& os) const {
}

void
ContentEncoding::parseRaw(const char* str, size_t len) {
    // TODO: case-insensitive
    //
    if (!strncmp(str, "gzip", len)) {
        encoding_ = Encoding::Gzip;
    }
    else if (!strncmp(str, "deflate", len)) {
        encoding_ = Encoding::Deflate;
    }
    else if (!strncmp(str, "compress", len)) {
        encoding_ = Encoding::Compress;
    }
    else if (!strncmp(str, "identity", len)) {
        encoding_ = Encoding::Identity;
    }
    else {
        encoding_ = Encoding::Unknown;
    }
}

void
ContentEncoding::write(std::ostream& os) const {
    os << encodingString(encoding_);
}

Server::Server(const std::vector<std::string>& tokens)
    : tokens_(tokens)
{ }

Server::Server(const std::string& token)
{
    tokens_.push_back(token);
}

Server::Server(const char* token)
{
    tokens_.emplace_back(token);
}

void
Server::parse(const std::string& data)
{
}

void
Server::write(std::ostream& os) const
{
    std::copy(std::begin(tokens_), std::end(tokens_),
                 std::ostream_iterator<std::string>(os, " "));
}

void
ContentType::parseRaw(const char* str, size_t len)
{
    mime_.parseRaw(str, len);
}

void
ContentType::write(std::ostream& os) const {
    os << mime_.toString();
}

} // namespace Header

} // namespace Http

} // namespace Net
