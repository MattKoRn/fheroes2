/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2021 - 2025                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <optional>
#include <thread>

#include "timing.h"

namespace
{
    using SteadyClock = std::chrono::steady_clock;

    SteadyClock::duration accumulatedPauseDuration{};
    std::optional<SteadyClock::time_point> pauseStarted;
}

namespace fheroes2
{
    std::chrono::steady_clock::time_point getPausableTimePoint()
    {
        const SteadyClock::time_point now = SteadyClock::now();
        if ( pauseStarted ) {
            return *pauseStarted - accumulatedPauseDuration;
        }

        return now - accumulatedPauseDuration;
    }

    void setApplicationTimingPaused( const bool paused )
    {
        if ( paused ) {
            if ( !pauseStarted ) {
                pauseStarted = SteadyClock::now();
            }
            return;
        }

        if ( pauseStarted ) {
            accumulatedPauseDuration += SteadyClock::now() - *pauseStarted;
            pauseStarted.reset();
        }
    }

    void delayforMs( const uint32_t delayMs )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( delayMs ) );
    }
}
