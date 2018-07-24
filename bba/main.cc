#include <map>
#include <vector>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

enum TokenType : int8_t
{
    TOK_LABEL,
    TOK_REF,
    TOK_U8,
    TOK_U16,
    TOK_U32
};

struct Stream
{
    std::string name;
    std::vector<TokenType> tokenTypes;
    std::vector<uint32_t> tokenValues;
    std::vector<std::string> tokenComments;
};

Stream stringStream;
std::vector<Stream> streams;
std::map<std::string, int> streamIndex;
std::vector<int> streamStack;

std::vector<int> labels;
std::map<std::string, int> labelIndex;

std::vector<std::string> preText, postText;

const char *skipWhitespace(const char *p)
{
    if (p == nullptr)
        return "";
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

int main(int argc, char **argv)
{
    bool verbose = false;
    bool bigEndian = false;
    bool writeC = false;
    char buffer[512];

    int opt;
    while ((opt = getopt(argc, argv, "vbc")) != -1)
    {
        switch (opt)
        {
        case 'v':
            verbose = true;
            break;
        case 'b':
            bigEndian = true;
            break;
        case 'c':
            writeC = true;
            break;
        default:
            assert(0);
        }
    }

    assert(optind+2 == argc);

    FILE *fileIn = fopen(argv[optind], "rt");
    assert(fileIn != nullptr);

    FILE *fileOut = fopen(argv[optind+1], writeC ? "wt" : "wb");
    assert(fileOut != nullptr);

    while (fgets(buffer, 512, fileIn) != nullptr)
    {
        std::string cmd = strtok(buffer, " \t\r\n");

        if (cmd == "pre") {
            const char *p = skipWhitespace(strtok(nullptr, "\r\n"));
            preText.push_back(p);
            continue;
        }

        if (cmd == "post") {
            const char *p = skipWhitespace(strtok(nullptr, "\r\n"));
            postText.push_back(p);
            continue;
        }

        if (cmd == "push") {
            const char *p = strtok(nullptr, " \t\r\n");
            if (streamIndex.count(p) == 0) {
                streamIndex[p] = streams.size();
                streams.resize(streams.size() + 1);
                streams.back().name = p;
            }
            streamStack.push_back(streamIndex.at(p));
            continue;
        }

        if (cmd == "pop") {
            streamStack.pop_back();
            continue;
        }

        if (cmd == "label" || cmd == "ref") {
            const char *label = strtok(nullptr, " \t\r\n");
            const char *comment = skipWhitespace(strtok(nullptr, "\r\n"));
            Stream &s = streams.at(streamStack.back());
            if (labelIndex.count(label) == 0) {
                labelIndex[label] = labels.size();
                labels.push_back(-1);
            }
            s.tokenTypes.push_back(cmd == "label" ? TOK_LABEL : TOK_REF);
            s.tokenValues.push_back(labelIndex.at(label));
            if (verbose)
                s.tokenComments.push_back(comment);
            continue;
        }

        if (cmd == "u8" || cmd == "u16" || cmd == "u32") {
            const char *value = strtok(nullptr, " \t\r\n");
            const char *comment = skipWhitespace(strtok(nullptr, "\r\n"));
            Stream &s = streams.at(streamStack.back());
            s.tokenTypes.push_back(cmd == "u8" ? TOK_U8 : cmd == "u16" ? TOK_U16 : TOK_U32);
            s.tokenValues.push_back(atoll(value));
            if (verbose)
                s.tokenComments.push_back(comment);
            continue;
        }

        if (cmd == "str") {
            const char *value = skipWhitespace(strtok(nullptr, "\r\n"));
            std::string label = std::string("str:") + value;
            Stream &s = streams.at(streamStack.back());
            if (labelIndex.count(label) == 0) {
                labelIndex[label] = labels.size();
                labels.push_back(-1);
            }
            s.tokenTypes.push_back(TOK_REF);
            s.tokenValues.push_back(labelIndex.at(label));
            if (verbose)
                s.tokenComments.push_back(value);
            stringStream.tokenTypes.push_back(TOK_LABEL);
            stringStream.tokenValues.push_back(labelIndex.at(label));
            while (1) {
                stringStream.tokenTypes.push_back(TOK_U8);
                stringStream.tokenValues.push_back(*value);
                if (*value == 0)
                    break;
                value++;
            }
            continue;
        }

        assert(0);
    }

    if (verbose) {
        printf("Constructed %d streams:\n", int(streams.size()));
        for (auto &s : streams)
            printf("    stream '%s' with %d tokens\n", s.name.c_str(), int(s.tokenTypes.size()));
    }

    assert(!streams.empty());
    assert(streamStack.empty());
    streams.push_back(Stream());
    streams.back().tokenTypes.swap(stringStream.tokenTypes);
    streams.back().tokenValues.swap(stringStream.tokenValues);

    int cursor = 0;
    for (auto &s : streams) {
        for (int i = 0; i < int(s.tokenTypes.size()); i++) {
            switch (s.tokenTypes[i])
            {
            case TOK_LABEL:
                labels[s.tokenValues[i]] = cursor;
                break;
            case TOK_REF:
                cursor += 4;
                break;
            case TOK_U8:
                cursor += 1;
                break;
            case TOK_U16:
                assert(cursor % 2 == 0);
                cursor += 2;
                break;
            case TOK_U32:
                assert(cursor % 4 == 0);
                cursor += 4;
                break;
            default:
                assert(0);
            }
        }
    }

    if (verbose) {
        printf("resolved positions for %d labels.\n", int(labels.size()));
        printf("total data (including strings): %.2f MB\n", double(cursor) / (1024*1024));
    }

    std::vector<uint8_t> data(cursor);

    cursor = 0;
    for (auto &s : streams) {
        for (int i = 0; i < int(s.tokenTypes.size()); i++) {
            uint32_t value = s.tokenValues[i];
            int numBytes = 0;

            switch (s.tokenTypes[i])
            {
            case TOK_LABEL:
                break;
            case TOK_REF:
                value = labels[value] - cursor;
                numBytes = 4;
                break;
            case TOK_U8:
                numBytes = 1;
                break;
            case TOK_U16:
                numBytes = 2;
                break;
            case TOK_U32:
                numBytes = 4;
                break;
            default:
                assert(0);
            }

            if (bigEndian) {
                switch (numBytes)
                {
                case 4:
                    data[cursor++] = value >> 24;
                    data[cursor++] = value >> 16;
                    /* fall-through */
                case 2:
                    data[cursor++] = value >> 8;
                    /* fall-through */
                case 1:
                    data[cursor++] = value;
                    /* fall-through */
                case 0:
                    break;
                default:
                    assert(0);
                }
            } else {
                switch (numBytes)
                {
                case 4:
                    data[cursor+3] = value >> 24;
                    data[cursor+2] = value >> 16;
                    /* fall-through */
                case 2:
                    data[cursor+1] = value >> 8;
                    /* fall-through */
                case 1:
                    data[cursor] = value;
                    /* fall-through */
                case 0:
                    break;
                default:
                    assert(0);
                }
                cursor += numBytes;
            }
        }
    }

    assert(cursor == int(data.size()));

    if (writeC) {
        for (auto &s : preText)
            fprintf(fileOut, "%s\n", s.c_str());

        fprintf(fileOut, "const char %s[%d] =\n\"", streams[0].name.c_str(), int(data.size())+1);

        cursor = 1;
        for (auto d : data) {
            if (cursor > 70) {
                fputc('\"', fileOut);
                fputc('\n', fileOut);
                cursor = 0;
            }
            if (cursor == 0) {
                fputc('\"', fileOut);
                cursor = 1;
            }
            if (d < 32 || d >= 128) {
                fprintf(fileOut, "\\%03o", int(d));
                cursor += 4;
            } else
            if (d == '\"' || d == '\'' || d == '\\') {
                fputc('\\', fileOut);
                fputc(d, fileOut);
                cursor += 2;
            } else {
                fputc(d, fileOut);
                cursor++;
            }
        }

        fprintf(fileOut, "\";\n");

        for (auto &s : postText)
            fprintf(fileOut, "%s\n", s.c_str());
    } else {
        fwrite(data.data(), int(data.size()), 1, fileOut);
    }

    return 0;
}
