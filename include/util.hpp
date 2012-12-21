#ifndef VZ_UTIL_HPP_
#define VZ_UTIL_HPP_

/**
 * @file util.hpp
 *
 * Utilities for enhancing the source code.
 */

/**
 * Checks format strings for correctness on printf-like functions.
 * Use in function declarations just before the semicolon.
 *
 * @param fmtidx Format string argument index, i.e., the number of the parameter being passed the format string argument.
 * @param argsidx Index of the first variadic argument, i.e., the number of the <code>...</code> parameter.
 *
 * @note Requires compiler support.
 */
#ifdef __GNUC__ // g++
#	define FORMAT_CHECK( fmtidx, argsidx ) __attribute__( ( format ( printf, ( fmtidx ), ( argsidx ) ) ) )
#else // unsupported compiler
#	define FORMAT_CHECK( fmtidx, argsidx ) /* no check */
#endif

/**
 * Warns if result is not used but should.
 * Use in function declarations just before the semicolon.
 *
 * @note Requires compiler support
 */
#ifdef __GNUC__ // g++
#	define WARN_UNUSED_RESULT __attribute__( ( warn_unused_result ) )
#else // unsupported compiler
#	define WARN_UNUSED_RESULT /* no warning */
#endif

#endif
