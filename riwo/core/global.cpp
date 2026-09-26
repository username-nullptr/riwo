// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "global.h"

#ifdef __unix__
# include <sys/types.h>
# include <unistd.h>
#elif _WIN32
# include <map>
#endif //__unix__

namespace riwo
{

const char *version_string()
{
	return RIWO_VERSION_STR;
}

const char *text_code()
{
#ifdef __unix__
	auto lang = std::getenv("LANG");
	if( not lang or lang[0] == '\0' )
		return "Unknown";
	return lang;
#elif _WIN32
	static std::map<UINT, const char*> map
	{
		{ 850  , "IBM 850 (Multilingual Latin 1; Western European)" },
		{ 866  , "CP866 (DOS Russian)"                              },
		{ 874  , "Windows-874 (Thai)"                               },
		{ 932  , "Shift_JIS (Japanese)"                             },
		{ 936  , "GBK (Simplified Chinese)"                         },
		{ 949  , "KS_C_5601-1987 (Korean)"                          },
		{ 950  , "Big5 (Traditional Chinese)"                       },
		{ 1200 , "UTF-16 (Little Endian)"                           },
		{ 1201 , "UTF-16 (Big Endian)"                              },
		{ 1250 , "Windows-1250 (Central European)"                  },
		{ 1251 , "Windows-1251 (Cyrillic)"                          },
		{ 1252 , "Windows-1252 (Western European)"                  },
		{ 1253 , "Windows-1253 (Greek)"                             },
		{ 1254 , "Windows-1254 (Turkish)"                           },
		{ 1255 , "Windows-1255 (Hebrew)"                            },
		{ 1256 , "Windows-1256 (Arabic)"                            },
		{ 1257 , "Windows-1257 (Baltic)"                            },
		{ 1258 , "Windows-1258 (Vietnamese)"                        },
		{ 1259 , "Windows-1259 (Latin 3)"                           },
		{ 10000, "MAC Roman"                                        },
		{ 10001, "MAC Japanese"                                     },
		{ 10002, "MAC Traditional Chinese (Big5)"                   },
		{ 10003, "MAC Korean"                                       },
		{ 10004, "MAC Arabic"                                       },
		{ 10005, "MAC Hebrew"                                       },
		{ 10006, "MAC Greek I"                                      },
		{ 10007, "MAC Cyrillic"                                     },
		{ 10008, "MAC Chinese Simplified (GB 2312)"                 },
		{ 10010, "MAC Romania"                                      },
		{ 10017, "MAC Ukrainian"                                    },
		{ 10021, "MAC Thai"                                         },
		{ 10029, "MAC Latin 2"                                      },
		{ 10079, "MAC Icelandic"                                    },
		{ 10081, "MAC Turkish"                                      },
		{ 10082, "MAC Croatian"                                     },
		{ 12000, "UTF-32 (Little Endian)"                           },
		{ 12001, "UTF-32 (Big Endian)"                              },
		{ 20127, "US-ASCII"                                         },
		{ 20866, "KOI8-R"                                           },
		{ 20932, "EUC-JP (Japanese)"                                },
		{ 20936, "GB2312 (Simplified Chinese)"                      },
		{ 20949, "KS_C_5601-1987 (Korean)"                          },
		{ 21866, "KOI8-U"                                           },
		{ 28591, "ISO-8859-1 (Latin 1; Western European)"           },
		{ 28592, "ISO-8859-2 (Latin 2; Central European)"           },
		{ 28593, "ISO-8859-3 (Latin 3)"                             },
		{ 28594, "ISO-8859-4 (Latin 4; Baltic)"                     },
		{ 28595, "ISO-8859-5 (Cyrillic)"                            },
		{ 28596, "ISO-8859-6 (Arabic)"                              },
		{ 28597, "ISO-8859-7 (Greek)"                               },
		{ 28598, "ISO-8859-8 (Hebrew; Visual)"                      },
		{ 28599, "ISO-8859-9 (Latin 5; Turkish)"                    },
		{ 28603, "ISO-8859-13 (Latin 7; Baltic Rim)"                },
		{ 28605, "ISO-8859-15 (Latin 9; Western European)"          },
		{ 65001, "UTF-8"                                            }
	};
	auto it = map.find(GetACP());
	return it == map.end() ? "Unknown" : it->second;
#else
	// TODO ... ...
	return "Unknown";
#endif
}

std::thread::id this_thread_id()
{
	return std::this_thread::get_id();
}

void forced_termination()
{
#ifdef __unix__
	kill(getpid(), SIGKILL);
#else
	TerminateProcess(GetCurrentProcess(), static_cast<UINT>(-9));
#endif
	// No return;
	abort(); // This function is never executed.
}

} //namespace riwo
