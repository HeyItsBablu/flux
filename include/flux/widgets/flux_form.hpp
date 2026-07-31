#ifndef FLUX_FORM_HPP
#define FLUX_FORM_HPP

#include "flux/flux_core.hpp"
#include "flux/flux_validatable.hpp"

#include <functional>
#include <vector>

// ============================================================================
// FormWidget
//
// A thin coordinator, not a primitive — it has no rendering opinion of its
// own beyond stacking its children in a column (via Box), and no idea what
// any individual field is. At submit() time it walks its own subtree,
// finds anything that implements Validatable (TextInputWidget today — any
// future widget opts in the same way), touches it to reveal its error
// state, and folds every field's validity into one aggregate result.
//
// v1 deliberately does NOT collect field values into a map — build fields
// as local variables, pass them into setChildren(), and read them back
// directly (e.g. emailInput->inputValue) after a successful submit().
//
//   auto email = TextInput("you@example.com")->setInputType(InputType::Email);
//   auto form  = Form();
//   form->setChildren({ email, Button("Submit", [form]{ form->submit(); }) })
//       ->setOnSubmit([email]{ /* email is valid here */ });
// ============================================================================

class FormWidget : public Widget
{
public:
    // Fired by submit() only when every validatable descendant is valid.
    std::function<void()> onValidSubmit;

    std::shared_ptr<FormWidget> setChildren(std::vector<WidgetPtr> fields)
    {
        fields_ = std::move(fields);
        markNeedsLayout();
        return std::static_pointer_cast<FormWidget>(shared_from_this());
    }

    std::shared_ptr<FormWidget> setDirection(FlexDirection d)
    {
        direction_ = d;
        markNeedsLayout();
        return std::static_pointer_cast<FormWidget>(shared_from_this());
    }

    std::shared_ptr<FormWidget> setGap(int g)
    {
        gap_ = g;
        markNeedsLayout();
        return std::static_pointer_cast<FormWidget>(shared_from_this());
    }

    std::shared_ptr<FormWidget> setOnSubmit(std::function<void()> fn)
    {
        onValidSubmit = std::move(fn);
        return std::static_pointer_cast<FormWidget>(shared_from_this());
    }

    WidgetPtr build() override
    {
        return Box({fields_})
            ->setDisplay(Display::Flex)
            ->setDirection(direction_)
            ->setGap(gap_);
    }

    // Touches every validatable field (revealing error state on invalid
    // ones) and returns whether the whole form is currently valid. Fires
    // onValidSubmit if set and the form turns out valid. Call this from a
    // submit button's onClick.
    bool submit()
    {
        bool ok = true;
        walk(this, [&](Widget *w)
             {
            if (auto *v = dynamic_cast<Validatable *>(w))
            {
                v->markTouched();
                ok = ok && v->isValid();
            } });

        if (ok && onValidSubmit)
            onValidSubmit();

        return ok;
    }

    // Read-only check — does NOT touch fields or reveal error state.
    // Handy for e.g. disabling a submit button before the user has tried.
    bool isFormValid() const
    {
        bool ok = true;
        walk(const_cast<FormWidget *>(this), [&](Widget *w)
             {
            if (auto *v = dynamic_cast<Validatable *>(w))
                ok = ok && v->isValid(); });
        return ok;
    }

private:
    std::vector<WidgetPtr> fields_;
    FlexDirection direction_ = FlexDirection::Column;
    int gap_ = 12;

    template <typename Fn>
    static void walk(Widget *w, Fn &&fn)
    {
        if (!w)
            return;
        fn(w);
        for (auto &child : w->children)
            walk(child.get(), fn);
    }
};

using FormWidgetPtr = std::shared_ptr<FormWidget>;

inline FormWidgetPtr Form()
{
    return std::make_shared<FormWidget>();
}

#endif // FLUX_FORM_HPP