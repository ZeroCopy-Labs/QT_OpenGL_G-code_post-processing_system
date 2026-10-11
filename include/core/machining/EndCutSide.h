#pragma once

namespace cadcam::machining
{
    // 加工断面的端面语义。
    // 断面：不做任何修正；左端面：只补正向差值（向 +X）；右端面：只补负向差值（向 −X）。
    // 定义放在工艺层，避免 core/machining 反向依赖 core/planning。
    enum class EndCutSide { Section, Left, Right };
}
