#ifndef SUBSCRIPTIONMANAGER_H
#define SUBSCRIPTIONMANAGER_H

// A simple manager to hold subscription status
class SubscriptionManager {
public:
    // Returns true if the user is subscribed (i.e., cloud services enabled)
    static bool currentSubscriptionStatus();
    // Set the subscription status (true for subscribed, false for standalone)
    static void setSubscriptionStatus(bool subscribed);

private:
    static bool isSubscribed; // Default is false (Standalone Mode)
};

#endif // SUBSCRIPTIONMANAGER_H
