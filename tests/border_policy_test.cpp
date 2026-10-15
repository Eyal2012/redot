#include "borderless_access.h"
#include <iostream>
struct Session { mutable int calls{}; void IsBorderRequired(bool required) const { if (required) throw 1; ++calls; } };
int main() {
    for (auto permission : {BorderPermission::Unrequested, BorderPermission::Pending, BorderPermission::Denied, BorderPermission::Unsupported, BorderPermission::Failed}) {
        Session session;
        if (applyBorderlessPolicy(permission, session) || session.calls) return 1;
    }
    for (int restart = 0; restart < 4; ++restart) {
        Session session;
        if (!applyBorderlessPolicy(BorderPermission::Allowed, session) || session.calls != 1) return 1;
    }
    std::cout << "PASS: simulated granted/denied border policy and four session recreations; this does not grant Windows consent or verify a visible border.\n";
}
