#include "Parameters.h"

namespace hamar::params
{
    const std::array<Spec, Count>& specs()
    {
        static const auto table = fjord::base::params::finish (std::array<Spec, Count> { HAMAR_PARAMS (FJORD_PARAM_SPEC) });
        return table;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        fjord::base::params::addToLayout (layout, specs());
        return layout;
    }
}
