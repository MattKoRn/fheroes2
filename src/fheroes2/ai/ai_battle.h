/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2024 - 2025                                             *
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

#pragma once

#include <algorithm>
#include <cstdint>

#include "color.h"

class HeroBase;
class Spell;

namespace Battle
{
    class Actions;
    class Arena;
    class Position;
    class Unit;
    class Units;
}

namespace AI
{
    struct BattleTargetPair
    {
        int cell{ -1 };
        const Battle::Unit * unit{ nullptr };
    };

    struct SpellSelection
    {
        int spellID{ -1 };
        int32_t cell{ -1 };
        double value{ 0.0 };
        int32_t destinationCell{ -1 };
    };

    struct SpellcastOutcome
    {
        int32_t cell{ -1 };
        double value{ 0.0 };
        int32_t destinationCell{ -1 };

        void updateOutcome( const double potentialValue, const int32_t targetCell, const bool isMassEffect = false )
        {
            if ( isMassEffect ) {
                value += potentialValue;
            }
            else if ( potentialValue > value ) {
                value = potentialValue;
                cell = targetCell;
            }
        }
    };

    class BattlePlanner
    {
    public:
        static BattlePlanner & Get();

        // Should be called at the beginning of the battle
        void battleBegins();

        void BattleTurn( Battle::Arena & arena, const Battle::Unit & currentUnit, Battle::Actions & actions );

    private:
        class CautiousOffensiveDecision
        {
        public:
            CautiousOffensiveDecision( const double & myArmyStrength, const double & enemyArmyStrength, const double & enemySpellStrength,
                                       const bool & considerRetreat, const PlayerColor & myColor, const uint32_t & currentTurnNumber )
                : _myArmyStrength( myArmyStrength )
                , _enemyArmyStrength( enemyArmyStrength )
                , _enemySpellStrength( enemySpellStrength )
                , _considerRetreat( considerRetreat )
                , _myColor( myColor )
                , _currentTurnNumber( currentTurnNumber )
            {}

            CautiousOffensiveDecision & operator=( const bool enemyHasLimitedRangedPressure )
            {
                // analyzeBattleState() first clears its per-turn strength fields and assigns false to this
                // decision. Ignore that reset marker: the second assignment arrives after the armies have
                // been measured and is the one that should update momentum and tactical posture.
                if ( _myArmyStrength <= 0.0 || _enemyArmyStrength <= 0.0 ) {
                    return *this;
                }

                const bool sideChanged = _hasHistory && _myColor != _previousColor;
                const bool battleTurnRestarted = _hasHistory && _currentTurnNumber < _lastTurnNumber;
                if ( sideChanged || battleTurnRestarted ) {
                    _hasHistory = false;
                    _momentum = 0.0;
                    _value = false;
                }

                if ( _hasHistory ) {
                    const double friendlyLossFraction
                        = std::max( 0.0, ( _previousMyArmyStrength - _myArmyStrength ) / std::max( 1.0, _previousMyArmyStrength ) );
                    const double enemyLossFraction
                        = std::max( 0.0, ( _previousEnemyArmyStrength - _enemyArmyStrength ) / std::max( 1.0, _previousEnemyArmyStrength ) );

                    // Favorable exchanges build momentum, while losing trades push the AI toward preservation.
                    // Decay keeps old exchanges from dominating the entire battle.
                    const double exchangeMomentum = ( enemyLossFraction - friendlyLossFraction ) * 1.5;
                    _momentum = std::clamp( _momentum * 0.60 + exchangeMomentum, -0.35, 0.35 );
                }

                _previousMyArmyStrength = _myArmyStrength;
                _previousEnemyArmyStrength = _enemyArmyStrength;
                _previousColor = _myColor;
                _lastTurnNumber = _currentTurnNumber;
                _hasHistory = true;

                const double relativeArmyStrength = _myArmyStrength / _enemyArmyStrength;
                const bool enemyHasMeaningfulSpellPressure = _enemySpellStrength > _myArmyStrength * 0.20;

                // Ranged or spell pressure forces tempo: waiting while the opponent can damage us safely is
                // not preservation, it is simply losing initiative.
                if ( !enemyHasLimitedRangedPressure || enemyHasMeaningfulSpellPressure ) {
                    _value = false;
                    return *this;
                }

                constexpr double cautiousEntryStrength = 1.10;
                constexpr double cautiousExitStrength = 1.35;
                constexpr double badMomentumThreshold = -0.04;
                constexpr double goodMomentumThreshold = 0.04;

                if ( _value ) {
                    // Once cautious, demand a real improvement before switching back to direct pressure.
                    // This wider exit threshold is the hysteresis band that prevents turn-to-turn thrashing.
                    const bool regainedInitiative = !_considerRetreat
                                                    && ( ( relativeArmyStrength >= cautiousExitStrength && _momentum >= -0.02 )
                                                         || ( relativeArmyStrength >= 1.15 && _momentum >= goodMomentumThreshold ) );
                    if ( regainedInitiative ) {
                        _value = false;
                    }
                }
                else {
                    // Enter preservation mode when materially weaker, after losing exchanges, or after the
                    // existing retreat analysis detects meaningful attrition.
                    if ( _considerRetreat || relativeArmyStrength < cautiousEntryStrength || _momentum <= badMomentumThreshold ) {
                        _value = true;
                    }
                }

                return *this;
            }

            operator bool() const
            {
                return _value;
            }

        private:
            const double & _myArmyStrength;
            const double & _enemyArmyStrength;
            const double & _enemySpellStrength;
            const bool & _considerRetreat;
            const PlayerColor & _myColor;
            const uint32_t & _currentTurnNumber;

            double _previousMyArmyStrength{ 0.0 };
            double _previousEnemyArmyStrength{ 0.0 };
            double _momentum{ 0.0 };
            PlayerColor _previousColor{ PlayerColor::NONE };
            uint32_t _lastTurnNumber{ 0 };
            bool _hasHistory{ false };
            bool _value{ false };
        };

        BattlePlanner()
            : _cautiousOffensive( _myArmyStrength, _enemyArmyStrength, _enemySpellStrength, _considerRetreat, _myColor, _currentTurnNumber )
        {}

        // Checks whether the limit of turns is exceeded for the attacking AI-controlled
        // hero and inserts an appropriate action to the action list if necessary
        bool isLimitOfTurnsExceeded( const Battle::Arena & arena, Battle::Actions & actions );

        Battle::Actions planUnitTurn( Battle::Arena & arena, const Battle::Unit & currentUnit );

        void analyzeBattleState( const Battle::Arena & arena, const Battle::Unit & currentUnit );

        Battle::Actions archerDecision( Battle::Arena & arena, const Battle::Unit & currentUnit ) const;

        BattleTargetPair meleeUnitOffense( Battle::Arena & arena, const Battle::Unit & currentUnit ) const;
        BattleTargetPair meleeUnitDefense( Battle::Arena & arena, const Battle::Unit & currentUnit ) const;

        bool isPositionLocatedInDefendedArea( const Battle::Unit & currentUnit, const Battle::Position & pos ) const;

        SpellSelection selectBestSpell( Battle::Arena & arena, const Battle::Unit & currentUnit, const bool retreating ) const;

        SpellcastOutcome spellDamageValue( const Spell & spell, Battle::Arena & arena, const Battle::Unit & currentUnit, const Battle::Units & friendly,
                                           const Battle::Units & enemies, bool retreating ) const;
        SpellcastOutcome spellDispelValue( const Spell & spell, const Battle::Units & friendly, const Battle::Units & enemies ) const;
        SpellcastOutcome spellResurrectValue( const Spell & spell, const Battle::Arena & arena ) const;
        SpellcastOutcome spellSummonValue( const Spell & spell, const Battle::Arena & arena, const PlayerColor heroColor ) const;
        SpellcastOutcome spellDragonSlayerValue( const Spell & spell, const Battle::Units & friendly, const Battle::Units & enemies ) const;
        SpellcastOutcome spellTeleportValue( Battle::Arena & arena, const Spell & spell, const Battle::Unit & currentUnit, const Battle::Units & enemies ) const;
        SpellcastOutcome spellEarthquakeValue( const Battle::Arena & arena, const Spell & spell, const Battle::Units & friendly ) const;
        SpellcastOutcome spellEffectValue( const Spell & spell, const Battle::Units & targets, const Battle::Units & enemies ) const;

        double spellEffectValue( const Spell & spell, const Battle::Unit & target, const Battle::Units & enemies, const bool targetIsLast, const bool forDispel ) const;
        double getSpellDisruptingRayRatio( const Battle::Unit & target ) const;
        double getSpellSlowRatio( const Battle::Unit & target ) const;
        double getSpellHasteRatio( const Battle::Unit & target ) const;
        int32_t spellDurationMultiplier( const Battle::Unit & target ) const;

        bool isSpellcastUselessForUnit( const Battle::Unit & unit, const Battle::Units & enemies, const Spell & spell ) const;

        static double getMeleeBestOutcome( Battle::Arena & arena, const Battle::Unit & currentUnit, const Battle::Units & enemies, BattleTargetPair & bestTarget );

        // When this limit of turns without deaths is exceeded for an attacking AI-controlled hero,
        // the auto combat should be interrupted (one way or another)
        static const uint32_t MAX_TURNS_WITHOUT_DEATHS{ 50 };

        // Member variables related to the logic of checking the limit of the number of turns
        uint32_t _currentTurnNumber{ 0 };
        uint32_t _numberOfRemainingTurnsWithoutDeaths{ MAX_TURNS_WITHOUT_DEATHS };
        uint32_t _attackerForceTotalNumberOfDeadUnits{ 0 };
        uint32_t _defenderForceTotalNumberOfDeadUnits{ 0 };

        // Member variables with a lifetime in one turn
        const HeroBase * _commander{ nullptr };
        PlayerColor _myColor{ PlayerColor::NONE };
        double _myArmyStrength{ 0.0 };
        double _enemyArmyStrength{ 0.0 };
        double _myShootersStrength{ 0.0 };
        double _enemyShootersStrength{ 0.0 };
        double _myRangedUnitsOnly{ 0.0 };
        double _enemyRangedUnitsOnly{ 0.0 };
        double _myArmyAverageSpeed{ 0.0 };
        double _enemyAverageSpeed{ 0.0 };
        double _enemySpellStrength{ 0.0 };
        bool _attackingCastle{ false };
        bool _defendingCastle{ false };
        bool _considerRetreat{ false };
        bool _defensiveTactics{ false };
        CautiousOffensiveDecision _cautiousOffensive;
        bool _avoidStackingUnits{ false };
    };
}
