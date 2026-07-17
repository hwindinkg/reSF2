/**
 * Fyber Android SDK
 * <p>
 * Copyright (c) 2016 Fyber. All rights reserved.
 */
package com.fyber.ads.banners;

/**
 * Represents the size of a banner
 */
public class BannerSize {

	public static final int FULL_SIZE = -1;  // -> LayoutParams.FILL_PARENT/MATCH_PARENT
	public static final int AUTO_SIZE = -2; // -> LayoutParams.WRAP_CONTENT

	public final static BannerSize FIXED_SIZE_320_50 = Builder.newBuilder().withWidth(320).withHeight(50).build();
	public final static BannerSize FIXED_HEIGHT_50   = Builder.newBuilder().withHeight(50).build();
	public final static BannerSize FIXED_HEIGHT_90   = Builder.newBuilder().withHeight(90).build();
	public final static BannerSize FLUID_SIZE        = Builder.newBuilder().withHeight(FULL_SIZE).build();
	public final static BannerSize SMART_SIZE        = Builder.newBuilder().build();

	private int width;
	private int height;

	private BannerSize(Builder builder) {
		width = builder.width;
		height = builder.height;
	}

	/**
	 * @return the width of this banner
	 */
	public int getWidth() {
		return width;
	}

	/**
	 * @return the height of this banner
	 */
	public int getHeight() {
		return height;
	}

	public static class Builder {
		private int width  = FULL_SIZE;
		private int height = AUTO_SIZE;

		private Builder() {
		}

		/**
		 * Creates a new {@link BannerSize.Builder builder}
		 *
		 * @return {@link BannerSize.Builder}
		 */
		public static Builder newBuilder() {
			return new Builder();
		}

		/**
		 * Sets the desired fixed width
		 *
		 * @param width the desired fixed width
		 * @return {@link BannerSize.Builder}
		 */
		public Builder withWidth(int width) {
			// FIXME: 26/02/16 add check for > size
			this.width = width;
			return this;
		}

		/**
		 * Sets the desired fixed height
		 *
		 * @param height the desired fixed height
		 * @return {@link BannerSize.Builder}
		 */
		public Builder withHeight(int height) {
			// FIXME: 26/02/16 add check for > size
			this.height = height;
			return this;
		}

		/**
		 * Builds a {@link BannerSize}
		 *
		 * @return {@link BannerSize}
		 */
		public BannerSize build() {
			return new BannerSize(this);
		}
	}

	@Override
	public boolean equals(Object o) {
		if (this == o) {
			return true;
		}
		if (o == null || getClass() != o.getClass()) {
			return false;
		}

		BannerSize that = (BannerSize) o;

		if (width != that.width) {
			return false;
		}

		return height == that.height;
	}

	@Override
	public int hashCode() {
		int result = width;
		result = 31 * result + height;
		return result;
	}

	@Override
	public String toString() {
		String width, height;

		if (this.width == BannerSize.FULL_SIZE) {
			width = "full_width ";
		} else if (this.width == BannerSize.AUTO_SIZE) {
			width = "smart_width ";
		} else  {
			width = String.valueOf(this.width);
		}

		if (this.height == BannerSize.FULL_SIZE) {
			height = " full_height";
		} else if (this.height == BannerSize.AUTO_SIZE) {
			height = " smart_height";
		} else  {
			height = String.valueOf(this.height);
		}

		return "(" + width + "x" + height + ")";
	}
}
