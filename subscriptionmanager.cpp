#include "subscriptionmanager.h"

bool SubscriptionManager::isSubscribed = false; // Default to Standalone Mode

bool SubscriptionManager::currentSubscriptionStatus() {
    return isSubscribed;
}

void SubscriptionManager::setSubscriptionStatus(bool subscribed) {
    isSubscribed = subscribed;
}
