/**
 * reverse/frida_hooks/hook_block.js
 *
 * Frida hooks for Shadow Fight 2 — Block, Movement, and AI logic.
 * Targets: ShadowFight2_android.bin (ARM) and ShadowFight2.s86 (x86).
 *
 * Usage:
 *   frida -U -f com.nekki.shadowfight2 -l hook_block.js --no-pause
 *   # or attach to running:
 *   frida -U -n shadowfight2 -l hook_block.js
 *
 * Hook addresses are for ShadowFight2.s86 (x86, base 0x10000000).
 * For Android ARM binary, add the appropriate offset.
 */

// ============================================================================
// Configuration
// ============================================================================

const CONFIG = {
    // Base addresses differ between x86 (s86) and ARM (android)
    // x86: ShadowFight2.s86 loads at 0x10000000
    // ARM: ShadowFight2_android.bin — needs S3E loader offset
    TARGET: 'x86',  // Change to 'arm' for Android binary

    // Key function addresses (x86 / ShadowFight2.s86)
    addrs: {
        // AI decision loop — called every 0.6-1.0s per bot
        AI_DECISION_LOOP:     0x10171d80,

        // Attack damage calculation
        INTERVAL_ATTACK_GET_FACTORS: 0x10115921,

        // Model action/animation
        MODEL_START_ACTION:   0x1015C540,
        MODEL_SET_CURRENT_NODE: 0x1015B530,
        MODEL_STEP:           0x10161ad0,  // Movement processing
        MODEL_SET_NEAREST_ENEMY: 0x101586F0,
        MODEL_GET_MODEL_ALIGN: 0x10159780,

        // Animation pipeline
        MODEL_ANIM_PLAY_INFO: 0x101650FC,
        MODEL_ANIM_GET_PLAYER_ANIM: 0x1016622A,
        MODEL_ANIM_MIRROR_NODES: 0x10164093,

        // Condition system
        CONDITION_INTERVAL_V8: 0x10086b90,

        // Interpolation/position
        INTERPOLATE_NODES:    0x10163F60,
        UPDATE_ANIM_FRAME:    0x10164F20,
        UPDATE_NODES:         0x10165C10,
        APPLY_INTERPOLATION:  0x10164C20,
        FINALIZE_POSITION:    0x101661D0,
    },

    // Model struct offsets (from README.md analysis)
    model: {
        OFFSET_ANIM_DATA:         0x20,
        OFFSET_CURRENT_ANIM:      0x40,
        OFFSET_FLAG_BYTE_50:      0x50,
        OFFSET_MIRRORED_FLAG:     0x54,   // 0xFF = left, 0x01 = right
        OFFSET_ANIM_FRAME_IDX:    0x58,
        OFFSET_ANIM_SUBFRAME:     0x60,
        OFFSET_ANIM_TYPE:         0x68,
        OFFSET_INTERP_FLAG:       0x7c,
        OFFSET_NODE_UPDATE_FLAG:  0x7d,
        OFFSET_MIRRORED_COPY:     0x7e,
        OFFSET_UPDATE_FLAG:       0x7f,
        OFFSET_POS_X:             0x80,
        OFFSET_INTERP_POS_X:      0x84,
        OFFSET_INTERP_POS_X_COPY: 0x88,
        OFFSET_SPEED_MULTIPLIER:  0xb4,
        OFFSET_POS_OFFSET_Y:      0xb8,
        OFFSET_COORD_X:           0xe0,   // XOR 0x80000000 for mirror
        OFFSET_COORD_X_ALT:       0xe4,
        OFFSET_ANIM_CONTAINER:    0xe8,
        OFFSET_NEAREST_ENEMY_COPY: 0x120,
        OFFSET_TIMER:             0x124,
        OFFSET_NEAREST_ENEMY:     0x190,
        OFFSET_MODEL_REF_CASE3:   0x220,
        OFFSET_FLAG_45C:          0x45c,
        OFFSET_MODEL_REF_CASE1:   0x588,
        OFFSET_MODEL_REF:         0x598,
    },

    // Enable/disable individual hook groups
    hooks: {
        ai_decisions: true,
        block_chance: true,
        movement: true,
        model_actions: true,
        conditions: true,
        attack_factors: true,
    }
};

// ============================================================================
// Helpers
// ============================================================================

function addr(offset) {
    return ptr(CONFIG.addrs[offset] || offset);
}

function hex(val) {
    return '0x' + val.toString(16).padStart(8, '0');
}

function readFloat(ptr, offset) {
    return ptr.add(offset).readFloat();
}

function readU32(ptr, offset) {
    return ptr.add(offset).readU32();
}

function readPtr(ptr, offset) {
    return ptr.add(offset).readPointer();
}

// ============================================================================
// Hook: AI Decision Loop (FUN_10171d80)
// ============================================================================

function hookAIDecisionLoop() {
    if (!CONFIG.hooks.ai_decisions) return;

    const target = addr('AI_DECISION_LOOP');
    console.log('[*] Hooking AI decision loop at ' + target);

    Interceptor.attach(target, {
        onEnter: function(args) {
            // Save context for onLeave
            this.model = args[0];
            this.context_arg = args[1];

            if (this.model.isNull()) return;

            // Read model state
            try {
                const posX = readFloat(this.model, CONFIG.model.OFFSET_POS_X);
                const mirrored = readU32(this.model, CONFIG.model.OFFSET_MIRRORED_FLAG);
                const animFrame = readU32(this.model, CONFIG.model.OFFSET_ANIM_FRAME_IDX);
                const animType = readU32(this.model, CONFIG.model.OFFSET_ANIM_TYPE);
                const enemyPtr = readPtr(this.model, CONFIG.model.OFFSET_NEAREST_ENEMY);

                console.log('[AI_DECISION] model=' + this.model +
                    ' pos_x=' + posX.toFixed(1) +
                    ' mirrored=' + hex(mirrored) +
                    ' frame=' + animFrame +
                    ' animType=' + animType +
                    ' enemy=' + enemyPtr);

                if (!enemyPtr.isNull()) {
                    const enemyX = readFloat(enemyPtr, CONFIG.model.OFFSET_POS_X);
                    const dist = Math.abs(enemyX - posX);
                    console.log('[AI_DECISION] enemy_pos_x=' + enemyX.toFixed(1) +
                        ' distance=' + dist.toFixed(1));
                }
            } catch(e) {
                console.log('[AI_DECISION] Error reading model: ' + e);
            }
        },
        onLeave: function(retval) {
            const decision = retval.toInt32();
            console.log('[AI_DECISION] -> decision=' + decision +
                ' (' + decodeAIAction(decision) + ')');
        }
    });
}

// ============================================================================
// Hook: Block Chance (part of UseDefense evaluation)
// Block is evaluated inside the AI decision loop.
// We hook IntervalAttack::getFactors to see block-related factors.
// ============================================================================

function hookBlockChance() {
    if (!CONFIG.hooks.block_chance) return;

    // Hook IntervalAttack::getFactors — this is where damage/defense
    // factors (including block) are computed.
    const target = addr('INTERVAL_ATTACK_GET_FACTORS');
    console.log('[*] Hooking IntervalAttack::getFactors at ' + target);

    Interceptor.attach(target, {
        onEnter: function(args) {
            this.attack = args[0];
            this.defender = args[1];

            if (this.attack.isNull() || this.defender.isNull()) return;

            try {
                // Log attack parameters
                console.log('[BLOCK/ATTACK] attack_model=' + this.attack +
                    ' defender_model=' + this.defender);

                // Read defender state to check if blocking
                const defMirrored = readU32(this.defender, CONFIG.model.OFFSET_MIRRORED_FLAG);
                const defAnimType = readU32(this.defender, CONFIG.model.OFFSET_ANIM_TYPE);
                const defFrame = readU32(this.defender, CONFIG.model.OFFSET_ANIM_FRAME_IDX);

                console.log('[BLOCK/ATTACK] defender: mirrored=' + hex(defMirrored) +
                    ' animType=' + defAnimType + ' frame=' + defFrame);
            } catch(e) {
                console.log('[BLOCK/ATTACK] Error: ' + e);
            }
        },
        onLeave: function(retval) {
            // Return value is the damage factor/result
            const result = retval.toInt32();
            console.log('[BLOCK/ATTACK] -> damage_factor=' + result);
        }
    });
}

// ============================================================================
// Hook: Movement (Model::step)
// ============================================================================

function hookMovement() {
    if (!CONFIG.hooks.movement) return;

    const target = addr('MODEL_STEP');
    console.log('[*] Hooking Model::step at ' + target);

    Interceptor.attach(target, {
        onEnter: function(args) {
            this.model = args[0];
            if (this.model.isNull()) return;

            try {
                const posX = readFloat(this.model, CONFIG.model.OFFSET_POS_X);
                const speed = readFloat(this.model, CONFIG.model.OFFSET_SPEED_MULTIPLIER);
                const interpX = readFloat(this.model, CONFIG.model.OFFSET_INTERP_POS_X);
                const coordX = readFloat(this.model, CONFIG.model.OFFSET_COORD_X);

                console.log('[MOVEMENT] model=' + this.model +
                    ' pos_x=' + posX.toFixed(1) +
                    ' speed=' + speed.toFixed(3) +
                    ' interp_x=' + interpX.toFixed(1) +
                    ' coord_x=' + coordX.toFixed(1));
            } catch(e) {
                console.log('[MOVEMENT] Error: ' + e);
            }
        },
        onLeave: function(retval) {
            if (this.model.isNull()) return;
            try {
                const newPos = readFloat(this.model, CONFIG.model.OFFSET_POS_X);
                console.log('[MOVEMENT] -> new_pos_x=' + newPos.toFixed(1));
            } catch(e) {}
        }
    });
}

// ============================================================================
// Hook: Model Actions
// ============================================================================

function hookModelActions() {
    if (!CONFIG.hooks.model_actions) return;

    // Model::startAction
    Interceptor.attach(addr('MODEL_START_ACTION'), {
        onEnter: function(args) {
            this.model = args[0];
            this.action = args[1];
            if (this.model.isNull()) return;

            try {
                console.log('[MODEL_ACTION] startAction: model=' + this.model +
                    ' action_ptr=' + this.action);
            } catch(e) {}
        }
    });

    // Model::setCurrentNode
    Interceptor.attach(addr('MODEL_SET_CURRENT_NODE'), {
        onEnter: function(args) {
            this.model = args[0];
            this.node = args[1];
            if (this.model.isNull()) return;

            try {
                const animType = readU32(this.model, CONFIG.model.OFFSET_ANIM_TYPE);
                console.log('[MODEL_ACTION] setCurrentNode: model=' + this.model +
                    ' node=' + this.node + ' animType=' + animType);
            } catch(e) {}
        }
    });

    // ModelAnimation::playInfo — the animation update chain
    Interceptor.attach(addr('MODEL_ANIM_PLAY_INFO'), {
        onEnter: function(args) {
            this.model = args[0];
            if (this.model.isNull()) return;

            try {
                const frame = readU32(this.model, CONFIG.model.OFFSET_ANIM_FRAME_IDX);
                const subframe = readU32(this.model, CONFIG.model.OFFSET_ANIM_SUBFRAME);
                const animType = readU32(this.model, CONFIG.model.OFFSET_ANIM_TYPE);
                console.log('[ANIM] playInfo: model=' + this.model +
                    ' frame=' + frame + '/' + subframe +
                    ' type=' + animType);
            } catch(e) {}
        }
    });
}

// ============================================================================
// Hook: Condition System
// ============================================================================

function hookConditions() {
    if (!CONFIG.hooks.conditions) return;

    // ConditionInterval::virtual_8 — interval condition checker
    Interceptor.attach(addr('CONDITION_INTERVAL_V8'), {
        onEnter: function(args) {
            this.self_ptr = args[0];
            this.context = args[1];

            try {
                const matchId = readU32(this.self_ptr, 0x14);
                const negate = this.self_ptr.add(0x0C).readU8();
                console.log('[CONDITION] Interval check: matchId=' + matchId +
                    ' negate=' + negate + ' context=' + this.context);
            } catch(e) {}
        },
        onLeave: function(retval) {
            const result = retval.toInt32();
            console.log('[CONDITION] -> ' + (result ? 'MATCH' : 'NO_MATCH'));
        }
    });
}

// ============================================================================
// Hook: Attack Factors
// ============================================================================

function hookAttackFactors() {
    if (!CONFIG.hooks.attack_factors) return;

    // Already hooked in hookBlockChance, but add finalize position hook
    // to track root motion after attacks
    Interceptor.attach(addr('FINALIZE_POSITION'), {
        onEnter: function(args) {
            this.model = args[0];
            if (this.model.isNull()) return;

            try {
                const posX = readFloat(this.model, CONFIG.model.OFFSET_POS_X);
                const coordX = readFloat(this.model, CONFIG.model.OFFSET_COORD_X);
                const coordXAlt = readFloat(this.model, CONFIG.model.OFFSET_COORD_X_ALT);

                console.log('[POSITION] finalize: pos_x=' + posX.toFixed(1) +
                    ' coord_x=' + coordX.toFixed(1) +
                    ' coord_x_alt=' + coordXAlt.toFixed(1));
            } catch(e) {}
        }
    });
}

// ============================================================================
// AI Action Decoder
// ============================================================================

function decodeAIAction(id) {
    const actions = {
        0: 'Idle',
        1: 'ForwardStep (approach)',
        2: 'ShortAttack (light)',
        3: 'HeavyAttack (kick)',
        4: 'BackStep (retreat)',
        5: 'Duck (block)',
        6: 'Special (jump/roll)',
    };
    return actions[id] || 'Unknown(' + id + ')';
}

// ============================================================================
// Memory Watch: Monitor block state changes
// ============================================================================

function watchBlockState(modelAddr) {
    // Watch the model's animation type field for block state transitions
    // Block is typically animType for "Duck" animation
    console.log('[*] Watching block state at model=' + hex(modelAddr));

    // Periodically check if the model enters block state
    setInterval(function() {
        try {
            const model = ptr(modelAddr);
            const animType = readU32(model, CONFIG.model.OFFSET_ANIM_TYPE);
            const frame = readU32(model, CONFIG.model.OFFSET_ANIM_FRAME_IDX);

            // Log only when animType changes
            if (animType !== watchBlockState._lastType) {
                console.log('[BLOCK_STATE] animType changed: ' +
                    watchBlockState._lastType + ' -> ' + animType +
                    ' frame=' + frame);
                watchBlockState._lastType = animType;
            }
        } catch(e) {}
    }, 50); // Check every 50ms
}
watchBlockState._lastType = -1;

// ============================================================================
// Main
// ============================================================================

function main() {
    console.log('====================================');
    console.log(' Shadow Fight 2 — Block/Movement/AI Hooks');
    console.log(' Target: ' + CONFIG.TARGET);
    console.log('====================================');

    // Install all hooks
    try { hookAIDecisionLoop(); } catch(e) { console.log('[!] AI hook failed: ' + e); }
    try { hookBlockChance(); } catch(e) { console.log('[!] Block hook failed: ' + e); }
    try { hookMovement(); } catch(e) { console.log('[!] Movement hook failed: ' + e); }
    try { hookModelActions(); } catch(e) { console.log('[!] Model hook failed: ' + e); }
    try { hookConditions(); } catch(e) { console.log('[!] Condition hook failed: ' + e); }
    try { hookAttackFactors(); } catch(e) { console.log('[!] Attack hook failed: ' + e); }

    console.log('[*] All hooks installed. Watching for block/movement/AI events...');
    console.log('[*] Tip: call watchBlockState(0xADDR) to monitor a specific model');
}

// Run immediately when loaded
main();
