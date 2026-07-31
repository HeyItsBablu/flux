#ifndef FLUX_VALIDATABLE_HPP
#define FLUX_VALIDATABLE_HPP

// ============================================================================
// Validatable
//
// Opt-in interface for widgets that have a notion of validity. Only
// widgets that actually need it (TextInputWidget today) inherit this IN
// ADDITION TO Widget — every other widget (Text, Box, Button, ...) is
// completely untouched by it. FormWidget finds validatable descendants at
// submit time via dynamic_cast<Validatable*>, so adding this to a new
// widget is all that's required to make it participate in a Form.
// ============================================================================

class Validatable
{
public:
    virtual ~Validatable() = default;

    // Current validity of this field.
    virtual bool isValid() const = 0;

    // Called when the form wants to reveal this field's validation state
    // (typically on submit) — e.g. flips a "touched" flag so an empty or
    // untouched field doesn't show as invalid before the user interacts
    // with it, but does once they've tried to submit.
    virtual void markTouched() = 0;
};

#endif // FLUX_VALIDATABLE_HPP