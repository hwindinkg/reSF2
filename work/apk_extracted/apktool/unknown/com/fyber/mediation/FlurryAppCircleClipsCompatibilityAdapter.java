package com.fyber.mediation;

import com.fyber.ads.banners.mediation.BannerMediationAdapter;
import com.fyber.mediation.flurry.FlurryMediationAdapter;

public final class FlurryAppCircleClipsCompatibilityAdapter extends FlurryMediationAdapter {
  public BannerMediationAdapter getBannerMediationAdapter() {
    return null;
  }
}
